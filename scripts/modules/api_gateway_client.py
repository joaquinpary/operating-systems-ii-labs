"""
API Gateway client module.

Public API:
    DEFAULT_BASE_URL        - default base URL for the gateway
    DEFAULT_JWT_SECRET      - default secret used to sign local JWTs
    ITEM_CATALOGUE          - supported shipment item catalogue
    generate_jwt(...)       - create a signed HS256 JWT
    create_shipment(...)    - POST /shipments
    dispatch_shipment(...)  - POST /dispatch
    get_all_statuses(...)   - GET /status
    get_status(...)         - GET /status/:id
    cancel_shipment_ws(...) - WS cancel_shipment flow
    listen_alerts(...)      - WS listener for emergency alerts
    chat_session(...)       - interactive WS session for alerts + cancel
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import socket
import ssl
import struct
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

DEFAULT_BASE_URL = "http://127.0.0.1:8081"
DEFAULT_JWT_SECRET = "change-me"
DEFAULT_CHAT_PATH = "/ws/chat"

# Resolve paths relative to project root
_MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(os.path.dirname(_MODULE_DIR))
GATEWAY_CREDS_DIR = os.path.join(_PROJECT_ROOT, "config", "gateway")

_WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
_WS_OPCODE_TEXT = 0x1
_WS_OPCODE_CLOSE = 0x8
_WS_OPCODE_PING = 0x9
_WS_OPCODE_PONG = 0xA
_WS_CLOSE_NORMAL = 1000

ITEM_CATALOGUE = [
    (1, "food"),
    (2, "water"),
    (3, "medicine"),
    (4, "tools"),
    (5, "guns"),
    (6, "ammo"),
]


def _b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def generate_jwt(
    secret: str,
    claims: dict[str, Any] | None = None,
    exp_hours: float = 24,
) -> str:
    header = {"alg": "HS256", "typ": "JWT"}
    payload: dict[str, Any] = {
        "sub": "tools-tester",
        "iat": int(time.time()),
        "exp": int(time.time() + exp_hours * 3600),
    }
    if claims:
        payload.update(claims)

    segments = (
        _b64url(json.dumps(header, separators=(",", ":")).encode()),
        _b64url(json.dumps(payload, separators=(",", ":")).encode()),
    )
    signing_input = f"{segments[0]}.{segments[1]}"
    signature = hmac.new(
        secret.encode(), signing_input.encode(), hashlib.sha256
    ).digest()
    return f"{signing_input}.{_b64url(signature)}"


def _request(
    method: str,
    url: str,
    token: str | None = None,
    payload: Any = None,
) -> dict[str, Any]:
    data = None
    headers = {"Accept": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url=url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            charset = resp.headers.get_content_charset("utf-8")
            body = resp.read().decode(charset)
            return json.loads(body) if body else {}
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body)
        except json.JSONDecodeError:
            parsed = {"error": body}
        parsed.setdefault("http_status", exc.code)
        return parsed
    except urllib.error.URLError as exc:
        return {"error": str(exc.reason)}
    except Exception as exc:
        return {"error": str(exc)}


def _print_response(resp: Any) -> None:
    print(json.dumps(resp, indent=2, ensure_ascii=False))


# ── Credential & login helpers ──────────────────────────────────────────────

def read_credential(conf_path: str) -> dict[str, str]:
    """Read username and password from a gateway .conf file."""
    cred: dict[str, str] = {}
    with open(conf_path) as f:
        for line in f:
            line = line.strip()
            if "=" in line:
                key, _, value = line.partition("=")
                cred[key.strip()] = value.strip()
    return cred


def list_credentials() -> list[str]:
    """Return sorted list of .conf file paths in the gateway credentials dir."""
    if not os.path.isdir(GATEWAY_CREDS_DIR):
        return []
    return sorted(
        os.path.join(GATEWAY_CREDS_DIR, f)
        for f in os.listdir(GATEWAY_CREDS_DIR)
        if f.endswith(".conf")
    )


def login(base_url: str, username: str, password: str) -> dict[str, Any]:
    """POST /login — authenticate and return a JWT token."""
    payload = {"username": username, "password": password}
    return _request("POST", f"{base_url}/login", payload=payload)


def _print_ws_message(message: Any) -> None:
    print("\n  [ws] incoming message:")
    _print_response(message)


def build_chat_url(base_url: str, token: str | None = None) -> str:
    parsed = urllib.parse.urlsplit(base_url)
    scheme = "wss" if parsed.scheme == "https" else "ws"
    netloc = parsed.netloc or parsed.path
    query_params = urllib.parse.parse_qsl(parsed.query, keep_blank_values=True)

    filtered_query = [(key, value) for key, value in query_params if key != "token"]
    if token:
        filtered_query.append(("token", token))

    return urllib.parse.urlunsplit(
        (scheme, netloc, DEFAULT_CHAT_PATH, urllib.parse.urlencode(filtered_query), "")
    )


class GatewayWebSocketClient:
    def __init__(self, url: str, timeout: float = 10.0) -> None:
        self.url = url
        self.timeout = timeout
        self._socket: socket.socket | ssl.SSLSocket | None = None
        self._write_lock = threading.Lock()
        self._closed = False
        self._recv_buffer = bytearray()

    def __enter__(self) -> "GatewayWebSocketClient":
        return self.connect()

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def connect(self) -> "GatewayWebSocketClient":
        if self._socket is not None:
            return self

        parsed = urllib.parse.urlsplit(self.url)
        if parsed.scheme not in {"ws", "wss"}:
            raise ValueError(f"unsupported websocket scheme: {parsed.scheme!r}")

        host = parsed.hostname or "127.0.0.1"
        port = parsed.port or (443 if parsed.scheme == "wss" else 80)
        path = parsed.path or "/"
        if parsed.query:
            path = f"{path}?{parsed.query}"

        raw_sock = socket.create_connection((host, port), timeout=self.timeout)
        if parsed.scheme == "wss":
            context = ssl.create_default_context()
            sock = context.wrap_socket(raw_sock, server_hostname=host)
        else:
            sock = raw_sock

        sock.settimeout(self.timeout)
        self._socket = sock
        self._recv_buffer.clear()
        try:
            self._perform_handshake(host, port, path)
        except Exception:
            self.close()
            raise

        return self

    def send_json(self, payload: dict[str, Any]) -> None:
        self._send_frame(
            _WS_OPCODE_TEXT,
            json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        )

    def send_cancel(self, shipment_id: str) -> None:
        self.send_json({
            "type": "cancel_shipment",
            "payload": {"shipment_id": shipment_id},
        })

    def recv_json(self, timeout: float | None = None) -> dict[str, Any] | None:
        sock = self._require_socket()
        previous_timeout = sock.gettimeout()
        if timeout is not None:
            sock.settimeout(timeout)

        try:
            while not self._closed:
                opcode, payload = self._recv_frame()
                if opcode == _WS_OPCODE_PING:
                    self._send_frame(_WS_OPCODE_PONG, payload)
                    continue
                if opcode == _WS_OPCODE_PONG:
                    continue
                if opcode == _WS_OPCODE_CLOSE:
                    self._closed = True
                    return None
                if opcode != _WS_OPCODE_TEXT:
                    continue

                text = payload.decode("utf-8")
                try:
                    return json.loads(text)
                except json.JSONDecodeError:
                    return {"type": "raw", "payload": text}
            return None
        finally:
            if timeout is not None and self._socket is not None:
                self._socket.settimeout(previous_timeout)

    def close(self) -> None:
        if self._socket is None:
            return

        sock = self._socket
        self._socket = None
        try:
            try:
                self._send_frame(
                    _WS_OPCODE_CLOSE,
                    struct.pack("!H", _WS_CLOSE_NORMAL),
                    socket_override=sock,
                )
            except OSError:
                pass
        finally:
            self._closed = True
            sock.close()

    def _perform_handshake(self, host: str, port: int, path: str) -> None:
        sock = self._require_socket()
        websocket_key = base64.b64encode(os.urandom(16)).decode("ascii")
        host_header = host if port in {80, 443} else f"{host}:{port}"
        request = "\r\n".join([
            f"GET {path} HTTP/1.1",
            f"Host: {host_header}",
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {websocket_key}",
            "Sec-WebSocket-Version: 13",
            "User-Agent: LoRa-CHADS-Tools/1.0",
            "",
            "",
        ]).encode("ascii")
        sock.sendall(request)

        raw_headers = self._recv_http_headers()
        header_text = raw_headers.decode("iso-8859-1")
        lines = header_text.split("\r\n")
        status_line = lines[0]
        if " 101 " not in status_line:
            raise ConnectionError(f"websocket upgrade failed: {status_line}")

        headers: dict[str, str] = {}
        for line in lines[1:]:
            if not line or ":" not in line:
                continue
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()

        expected_accept = base64.b64encode(
            hashlib.sha1(f"{websocket_key}{_WS_GUID}".encode("ascii")).digest()
        ).decode("ascii")
        if headers.get("sec-websocket-accept") != expected_accept:
            raise ConnectionError("websocket handshake rejected by server")

    def _recv_http_headers(self) -> bytes:
        sock = self._require_socket()
        buffer = bytearray()
        while b"\r\n\r\n" not in buffer:
            chunk = sock.recv(4096)
            if not chunk:
                raise ConnectionError("connection closed during websocket handshake")
            buffer.extend(chunk)
            if len(buffer) > 16 * 1024:
                raise ConnectionError("websocket handshake headers too large")
        header_block, _, tail = buffer.partition(b"\r\n\r\n")
        self._recv_buffer.extend(tail)
        return bytes(header_block)

    def _send_frame(
        self,
        opcode: int,
        payload: bytes = b"",
        socket_override: socket.socket | ssl.SSLSocket | None = None,
    ) -> None:
        sock = socket_override or self._require_socket()
        frame = bytearray([0x80 | opcode])
        payload_length = len(payload)
        mask_key = os.urandom(4)

        if payload_length < 126:
            frame.append(0x80 | payload_length)
        elif payload_length < (1 << 16):
            frame.append(0x80 | 126)
            frame.extend(struct.pack("!H", payload_length))
        else:
            frame.append(0x80 | 127)
            frame.extend(struct.pack("!Q", payload_length))

        masked = bytes(byte ^ mask_key[index % 4] for index, byte in enumerate(payload))
        frame.extend(mask_key)
        frame.extend(masked)

        with self._write_lock:
            sock.sendall(frame)

    def _recv_frame(self) -> tuple[int, bytes]:
        sock = self._require_socket()
        header = self._recv_exact(sock, 2)
        opcode = header[0] & 0x0F
        masked = (header[1] & 0x80) != 0
        payload_length = header[1] & 0x7F

        if payload_length == 126:
            payload_length = struct.unpack("!H", self._recv_exact(sock, 2))[0]
        elif payload_length == 127:
            payload_length = struct.unpack("!Q", self._recv_exact(sock, 8))[0]

        masking_key = self._recv_exact(sock, 4) if masked else b""
        payload = self._recv_exact(sock, payload_length) if payload_length else b""

        if masked:
            payload = bytes(
                byte ^ masking_key[index % 4] for index, byte in enumerate(payload)
            )

        return opcode, payload

    @staticmethod
    def _recv_exact_from_socket(sock: socket.socket | ssl.SSLSocket, size: int) -> bytes:
        buffer = bytearray()
        while len(buffer) < size:
            chunk = sock.recv(size - len(buffer))
            if not chunk:
                raise ConnectionError("connection closed while reading websocket frame")
            buffer.extend(chunk)
        return bytes(buffer)

    def _recv_exact(self, sock: socket.socket | ssl.SSLSocket, size: int) -> bytes:
        if size <= 0:
            return b""

        buffer = bytearray()
        if self._recv_buffer:
            take = min(size, len(self._recv_buffer))
            buffer.extend(self._recv_buffer[:take])
            del self._recv_buffer[:take]

        if len(buffer) < size:
            buffer.extend(self._recv_exact_from_socket(sock, size - len(buffer)))

        return bytes(buffer)

    def _require_socket(self) -> socket.socket | ssl.SSLSocket:
        if self._socket is None:
            raise ConnectionError("websocket client is not connected")
        return self._socket


def open_ws(base_url: str, token: str | None, timeout: float = 10.0) -> GatewayWebSocketClient:
    client = GatewayWebSocketClient(build_chat_url(base_url, token), timeout=timeout)
    return client.connect()


def build_items(quantities: dict[int, int]) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    for item_id, item_name in ITEM_CATALOGUE:
        qty = quantities.get(item_id, 0)
        if qty <= 0:
            continue
        items.append({
            "item_id": item_id,
            "item_name": item_name,
            "quantity": qty,
        })
    return items


def create_shipment(base_url: str, token: str | None, quantities: dict[int, int]) -> dict[str, Any]:
    resp = _request("POST", f"{base_url}/shipments", token, {"items": build_items(quantities)})
    _print_response(resp)
    return resp


def dispatch_shipment(base_url: str, token: str | None, shipment_id: str) -> dict[str, Any]:
    resp = _request("POST", f"{base_url}/dispatch", token, {"shipment_id": shipment_id})
    _print_response(resp)
    return resp


def get_all_statuses(base_url: str, token: str | None) -> Any:
    resp = _request("GET", f"{base_url}/status", token)
    _print_response(resp)
    return resp


def get_status(base_url: str, token: str | None, shipment_id: str) -> dict[str, Any]:
    resp = _request("GET", f"{base_url}/status/{shipment_id}", token)
    _print_response(resp)
    return resp


def cancel_shipment_ws(
    base_url: str,
    token: str | None,
    shipment_id: str,
    receive_timeout: float = 5.0,
) -> dict[str, Any]:
    with open_ws(base_url, token) as client:
        print(f"  Connected to {client.url}")
        client.send_cancel(shipment_id)

        deadline = time.monotonic() + max(receive_timeout, 0.1)
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            try:
                event = client.recv_json(timeout=max(0.1, remaining))
            except socket.timeout:
                break

            if event is None:
                break

            _print_ws_message(event)
            return event

    response = {"error": "no websocket response received"}
    _print_response(response)
    return response


def listen_alerts(
    base_url: str,
    token: str | None,
    seconds: float = 30.0,
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    duration = None if seconds <= 0 else seconds

    with open_ws(base_url, token) as client:
        print(f"  Listening on {client.url}")
        print("  Press Ctrl+C to stop early.")
        deadline = None if duration is None else time.monotonic() + duration

        while True:
            if deadline is not None and time.monotonic() >= deadline:
                break

            timeout = 1.0
            if deadline is not None:
                timeout = min(timeout, max(0.1, deadline - time.monotonic()))

            try:
                event = client.recv_json(timeout=timeout)
            except socket.timeout:
                continue

            if event is None:
                break

            events.append(event)
            _print_ws_message(event)

    return events


def chat_session(base_url: str, token: str | None) -> None:
    with open_ws(base_url, token) as client:
        print(f"  Connected to {client.url}")
        print("  Commands: /help, /cancel <shipment_id>, /quit")
        print("  Alerts and cancel responses will appear while this session is open.")

        stop_event = threading.Event()

        def _reader() -> None:
            while not stop_event.is_set():
                try:
                    event = client.recv_json(timeout=1.0)
                except socket.timeout:
                    continue
                except (ConnectionError, OSError) as exc:
                    if not stop_event.is_set():
                        print(f"\n  [ws] connection error: {exc}")
                    stop_event.set()
                    return

                if event is None:
                    print("\n  [ws] connection closed by server")
                    stop_event.set()
                    return

                _print_ws_message(event)

        reader = threading.Thread(target=_reader, daemon=True)
        reader.start()

        try:
            while not stop_event.is_set():
                raw = input("  ws> ").strip()
                if not raw:
                    continue
                if raw in {"/quit", "/exit"}:
                    break
                if raw == "/help":
                    print("  /cancel <shipment_id>  sends a cancel request over the active websocket.")
                    print("  /quit or /exit         closes the session.")
                    print("  Leaving the prompt idle keeps the session listening for alerts.")
                    continue
                if raw.startswith("/cancel"):
                    parts = raw.split(maxsplit=1)
                    if len(parts) != 2 or not parts[1].strip():
                        print("  Usage: /cancel <shipment_id>")
                        continue

                    client.send_cancel(parts[1].strip())
                    continue

                print("  Unknown command. Use /help.")
        finally:
            stop_event.set()
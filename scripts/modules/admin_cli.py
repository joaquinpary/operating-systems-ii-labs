"""
Admin CLI module — TCP-based admin REPL.

Public API:
    CLISession  — encapsulates connection state, auth, and REPL
    find_cli_conf() / find_client_conf(role, number)  — credential helpers
"""

import json
import os
import glob
import re
import socket
import threading
from datetime import datetime, timezone

SERVER_HOST = '127.0.0.1'
SERVER_PORT = 9999
BUF_SIZE = 1024

# ── config helpers ──────────────────────────────────────────────────────────

_MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(os.path.dirname(_MODULE_DIR))


def _load_conf(path):
    conf = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                key, value = line.split('=', 1)
                conf[key.strip()] = value.strip()
    return conf


def _get_clients_dir():
    return os.path.join(_PROJECT_ROOT, 'config', 'clients')


def find_cli_conf():
    """Return (conf_dict, conf_path) for the cli_admin credential."""
    clients_dir = _get_clients_dir()
    path = os.path.join(clients_dir, 'cli_admin.conf')
    if os.path.exists(path):
        return _load_conf(path), path
    raise FileNotFoundError(
        f"{path} not found — run Generate Credentials first"
    )


def find_client_conf(role, number=None):
    """Return (conf_dict, conf_path) for a hub or warehouse client.

    Args:
        role: 'hub' or 'wh'/'warehouse'
        number: optional client number
    """
    clients_dir = _get_clients_dir()
    target_type = 'WAREHOUSE' if role in ('wh', 'warehouse') else 'HUB'

    if number is not None:
        path = os.path.join(clients_dir, f'client_{number:04d}.conf')
        if os.path.exists(path):
            return _load_conf(path), path
        raise FileNotFoundError(f"{path} not found")

    for path in sorted(glob.glob(os.path.join(clients_dir, '*.conf'))):
        conf = _load_conf(path)
        if conf.get('type') == target_type:
            return conf, path

    raise FileNotFoundError(
        f"No {target_type} client found in {clients_dir}"
    )


# ── CLISession ──────────────────────────────────────────────────────────────

class CLISession:
    """Encapsulates a single admin CLI session (connect → auth → REPL → close)."""

    def __init__(self, client_id, client_type, username, password,
                 host=SERVER_HOST, port=SERVER_PORT):
        self.client_id = client_id
        self.client_type = client_type
        self.username = username
        self.password = password
        self.host = host
        self.port = port

        self.sock = None
        self.running = False
        self.authenticated = False

        # pagination
        self.last_command = None
        self.last_args = ""
        self.current_page = 1
        self.total_pages = 1

    # ── protocol ────────────────────────────────────────────────────────

    @staticmethod
    def _checksum(json_str):
        return f"{sum(json_str.encode()) % 0xFFFFFF:X}"

    def _create_message(self, msg_type, payload):
        ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] + 'Z'
        msg = {
            "msg_type": msg_type,
            "source_role": self.client_type,
            "source_id": self.client_id,
            "target_role": "SERVER",
            "target_id": "SERVER",
            "timestamp": ts,
            "payload": payload,
        }
        json_str = json.dumps(msg, separators=(',', ':'))
        msg["checksum"] = self._checksum(json_str)
        return json.dumps(msg, separators=(',', ':'))

    def _send(self, raw):
        buf = bytearray(BUF_SIZE)
        encoded = raw.encode()
        buf[:len(encoded)] = encoded
        self.sock.sendall(buf)

    # ── listener thread ─────────────────────────────────────────────────

    def _listener(self):
        while self.running:
            try:
                self.sock.settimeout(1.0)
                data = self.sock.recv(BUF_SIZE)
                if not data:
                    print("\n  [INFO] Server closed connection")
                    self.running = False
                    break

                text = data.rstrip(b'\x00').decode()
                try:
                    obj = json.loads(text)
                except json.JSONDecodeError:
                    print(f"\n  [RAW] {text}")
                    print("  > ", end='', flush=True)
                    continue

                msg_type = obj.get("msg_type", "")

                if "AUTH_RESPONSE" in msg_type:
                    payload = obj.get("payload", {})
                    code = payload.get("status_code", 0)
                    if code == 200:
                        self.authenticated = True
                        print("\n  [OK] Authenticated — admin CLI ready")
                    else:
                        print(f"\n  [FAIL] Auth rejected (code {code}): "
                              f"{payload.get('message', '')}")
                    print("  > ", end='', flush=True)
                    continue

                payload = obj.get("payload", {})
                if isinstance(payload, dict) and "status" in payload:
                    self._print_response(payload)
                elif "status" in obj:
                    self._print_response(obj)
                else:
                    print(f"\n  [SERVER] {json.dumps(obj, indent=2)}")

                print("  > ", end='', flush=True)

            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"\n  [ERROR] {e}")
                break

    def _print_response(self, resp):
        status = resp.get("status", "?")
        if status == "error":
            print(f"\n  [ERROR] {resp.get('message', 'unknown error')}")
            return

        if "page" in resp:
            self.current_page = resp["page"]
            self.total_pages = resp.get("total_pages", 1)

        skip_keys = {"status", "page", "total_pages", "total"}
        for key, value in resp.items():
            if key in skip_keys:
                continue
            if isinstance(value, list):
                print(f"\n  [{key.upper()}] ({len(value)} rows)")
                for item in value:
                    if isinstance(item, dict):
                        parts = [f"{k}={v}" for k, v in item.items()]
                        print(f"    {', '.join(parts)}")
                    else:
                        print(f"    {item}")
            elif isinstance(value, dict):
                print(f"\n  [{key.upper()}]")
                for k, v in value.items():
                    print(f"    {k}: {v}")
            else:
                print(f"\n  [{key.upper()}] {value}")

        if "page" in resp:
            total = resp.get("total", "?")
            print(f"\n  Page {self.current_page}/{self.total_pages} "
                  f"({total} total) — 'n' next, 'p' prev")

    # ── commands ────────────────────────────────────────────────────────

    def _send_admin(self, command, args=""):
        raw = self._create_message(
            "CLI_TO_SERVER__ADMIN_COMMAND",
            {"command": command, "args": args},
        )
        self._send(raw)

    def _send_auth(self):
        type_map = {
            "CLI": "CLI_TO_SERVER__AUTH_REQUEST",
            "HUB": "HUB_TO_SERVER__AUTH_REQUEST",
        }
        msg_type = type_map.get(self.client_type,
                                "WAREHOUSE_TO_SERVER__AUTH_REQUEST")
        raw = self._create_message(
            msg_type, {"username": self.username, "password": self.password}
        )
        self._send(raw)
        print(f"  [SENT] {msg_type}")

    @staticmethod
    def _replace_page(args, new_page):
        if re.search(r'\bpage\s+\d+', args):
            return re.sub(r'\bpage\s+\d+', f'page {new_page}', args)
        suffix = f" page {new_page}" if args else f"page {new_page}"
        return args + suffix

    # ── public entry point ──────────────────────────────────────────────

    def run(self):
        """Connect, authenticate, and enter the REPL.

        Returns when the user types 'quit' or the connection is lost.
        """
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.sock.connect((self.host, self.port))
        except ConnectionRefusedError:
            print(f"  [ERROR] Could not connect to {self.host}:{self.port}")
            return

        self.running = True
        print(f"  [CONNECTED] {self.host}:{self.port}")

        recv_thread = threading.Thread(target=self._listener, daemon=True)
        recv_thread.start()

        self._send_auth()

        try:
            while self.running:
                try:
                    line = input("  > ").strip()
                except EOFError:
                    break
                if not line:
                    continue

                parts = line.split(maxsplit=1)
                cmd = parts[0].lower()
                args = parts[1] if len(parts) > 1 else ""

                if cmd == "quit":
                    break
                elif cmd in ("n", "next"):
                    if self.last_command and self.current_page < self.total_pages:
                        new_args = self._replace_page(self.last_args,
                                                      self.current_page + 1)
                        self._send_admin(self.last_command, new_args)
                        self.last_args = new_args
                    else:
                        msg = ("No next page" if self.last_command
                               else "No previous query")
                        print(f"  {msg}")
                elif cmd in ("p", "prev"):
                    if self.last_command and self.current_page > 1:
                        new_args = self._replace_page(self.last_args,
                                                      self.current_page - 1)
                        self._send_admin(self.last_command, new_args)
                        self.last_args = new_args
                    else:
                        msg = ("No previous page" if self.last_command
                               else "No previous query")
                        print(f"  {msg}")
                elif cmd in ("help", "clients", "inventory", "transactions"):
                    if not self.authenticated:
                        print("  [WARN] Not authenticated yet — "
                              "server may reject the command")
                    self.last_command = cmd
                    self.last_args = args
                    self._send_admin(cmd, args)
                else:
                    print(f"  Unknown command: {cmd}")
                    print("  Commands: help, clients, inventory, "
                          "transactions, n, p, quit")
        except KeyboardInterrupt:
            pass
        finally:
            self.running = False
            self.sock.close()
            print("\n  [DISCONNECTED]")

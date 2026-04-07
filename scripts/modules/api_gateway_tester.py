"""
API Gateway interactive tester module.

Public API:
    DEFAULT_GW_URL          — default base URL for the gateway
    generate_jwt(secret, claims, exp_hours) — create a signed JWT token
    create_shipment(base_url, token, items)
    dispatch_shipment(base_url, token, shipment_id)
    get_all_statuses(base_url, token)
    get_status(base_url, token, shipment_id)
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import time
import urllib.error
import urllib.request
from typing import Any

DEFAULT_GW_URL = "http://127.0.0.1:8081"
DEFAULT_JWT_SECRET = "change-me"

# Resolve paths relative to project root
_MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(os.path.dirname(_MODULE_DIR))
GATEWAY_CREDS_DIR = os.path.join(_PROJECT_ROOT, "config", "gateway")

# Fixed item catalogue — mirrors the C++ server (api_gateway.c / json_manager.h)
ITEM_CATALOGUE = [
    (1, "food"),
    (2, "water"),
    (3, "medicine"),
    (4, "tools"),
    (5, "guns"),
    (6, "ammo"),
]


# ── JWT helpers ─────────────────────────────────────────────────────────────

def _b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def generate_jwt(
    secret: str,
    claims: dict[str, Any] | None = None,
    exp_hours: float = 24,
) -> str:
    """Generate an HS256-signed JWT token."""
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


# ── Credential helpers ──────────────────────────────────────────────────────

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
    resp = _request("POST", f"{base_url}/login", payload=payload)
    return resp


# ── HTTP helpers ────────────────────────────────────────────────────────────

def _request(
    method: str,
    url: str,
    token: str | None = None,
    payload: Any = None,
) -> dict[str, Any]:
    data = None
    headers: dict[str, str] = {"Accept": "application/json"}
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


# ── Public commands ─────────────────────────────────────────────────────────

def build_items(quantities: dict[int, int]) -> list[dict[str, Any]]:
    """Build the items array from a {item_id: quantity} mapping.

    Every catalogue item is included; items with quantity 0 are kept so the
    C++ server always receives the full QUANTITY_ITEMS vector.
    """
    items: list[dict[str, Any]] = []
    for item_id, item_name in ITEM_CATALOGUE:
        qty = quantities.get(item_id, 0)
        items.append({
            "item_id": item_id,
            "item_name": item_name,
            "quantity": qty,
        })
    return items


def create_shipment(
    base_url: str,
    token: str | None,
    quantities: dict[int, int],
) -> dict[str, Any]:
    """POST /shipments — create a new shipment.

    quantities: mapping of item_id (1-6) → quantity.
    """
    items = build_items(quantities)
    payload = {"items": items}
    resp = _request("POST", f"{base_url}/shipments", token, payload)
    _print_response(resp)
    return resp


def dispatch_shipment(
    base_url: str,
    token: str | None,
    shipment_id: str,
) -> dict[str, Any]:
    """POST /dispatch — dispatch an existing shipment."""
    payload = {"shipment_id": shipment_id}
    resp = _request("POST", f"{base_url}/dispatch", token, payload)
    _print_response(resp)
    return resp


def get_all_statuses(base_url: str, token: str | None) -> Any:
    """GET /status — list all tracked shipment statuses."""
    resp = _request("GET", f"{base_url}/status", token)
    _print_response(resp)
    return resp


def get_status(base_url: str, token: str | None, shipment_id: str) -> dict[str, Any]:
    """GET /status/:id — query a single shipment from the core."""
    resp = _request("GET", f"{base_url}/status/{shipment_id}", token)
    _print_response(resp)
    return resp

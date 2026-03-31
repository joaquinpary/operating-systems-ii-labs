"""
REST API client module.

Public API:
    request_json(method, url, payload=None) — generic HTTP JSON request
    load_json_file(file_path)               — read a JSON file
    upload_map(base_url, file_path)         — POST /map from file
    flow_capacity(base_url, source, sink)   — POST /request/fulfillment-flow
    circuit_solver(base_url, start)         — POST /request/fulfillment-circuit
    get_results(base_url)                   — GET /results
    build_generated_map(...)                — build a random in-memory map payload
    generate_map(base_url, *, nodes, density, active_prob, secure_prob,
                 output, upload)            — generate & optionally upload map
"""

from __future__ import annotations

import json
import random
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

DEFAULT_BASE_URL = "http://127.0.0.1:8080"


def load_json_file(file_path: str) -> Any:
    return json.loads(Path(file_path).read_text(encoding="utf-8"))


def request_json(method: str, url: str, payload: Any = None) -> dict[str, Any]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(
        url=url, data=data, headers=headers, method=method
    )
    try:
        with urllib.request.urlopen(request, timeout=3600) as response:
            charset = response.headers.get_content_charset("utf-8")
            body = response.read().decode(charset)
            return json.loads(body) if body else {}
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body)
        except json.JSONDecodeError:
            parsed = {"status": "error", "message": body}
        parsed.setdefault("http_status", exc.code)
        return parsed
    except urllib.error.URLError as exc:
        return {"status": "error", "message": str(exc.reason)}
    except Exception as exc:  # pragma: no cover - defensive fallback for CLI usage
        return {"status": "error", "message": str(exc)}


def _print_response(resp: dict[str, Any]) -> None:
    print(json.dumps(resp, indent=2, ensure_ascii=False))


def _print_error(message: str, *, silent: bool) -> None:
    if not silent:
        print(f"  Error: {message}", file=sys.stderr)


def build_generated_map(
    *,
    nodes: int = 10,
    density: float = 0.3,
    active_prob: float = 0.9,
    secure_prob: float = 0.9,
) -> list[dict[str, Any]]:
    tags_pool = ["food", "ammo", "medical", "fuel", "tools"]
    conn_types = [
        "road", "rail", "trail", "tunnel", "bridge",
        "waterway", "drone", "blocked", "manual",
    ]
    cond_types = [
        "infected_activity", "weather_rain", "foggy_visibility",
        "cleared", "reinforced",
    ]

    node_list: list[dict[str, Any]] = []
    for i in range(nodes):
        node_id = f"N{i:03d}"
        node_tags = random.sample(tags_pool, k=random.randint(0, len(tags_pool)))
        node = {
            "node_id": node_id,
            "node_name": f"Generated Node {i}",
            "node_type": "fulfillment_center",
            "node_description": "Randomly generated testing node",
            "node_location": {
                "latitude": round(random.uniform(-90.0, 90.0), 4),
                "longitude": round(random.uniform(-180.0, 180.0), 4),
            },
            "node_tags": node_tags,
            "is_secure": random.random() < secure_prob,
            "is_active": random.random() < active_prob,
            "connections": [],
            "schema": {"version": 1},
        }
        node_list.append(node)

    for i, node in enumerate(node_list):
        for j in range(nodes):
            if i != j and random.random() < density:
                dest_id = f"N{j:03d}"
                conds = random.sample(
                    cond_types, k=random.randint(0, len(cond_types))
                )
                edge = {
                    "to": dest_id,
                    "connection_type": random.choice(conn_types),
                    "base_weight": round(random.uniform(1.0, 100.0), 2),
                    "connection_conditions": conds,
                }
                node["connections"].append(edge)

    return node_list


# ── Commands ────────────────────────────────────────────────────────────────

def upload_map(base_url: str, file_path: str, *, silent: bool = False) -> dict[str, Any]:
    """Upload a map JSON file to /map."""
    try:
        payload = load_json_file(file_path)
    except Exception as exc:
        message = f"Error reading {file_path}: {exc}"
        _print_error(message, silent=silent)
        return {"status": "error", "message": message}
    resp = request_json("POST", f"{base_url}/map", payload)
    if not silent:
        _print_response(resp)
    return resp


def flow_capacity(base_url: str, source: str, sink: str, *, silent: bool = False) -> dict[str, Any]:
    """Calculate max flow between source and sink."""
    payload = {"source": source, "sink": sink}
    resp = request_json("POST", f"{base_url}/request/fulfillment-flow", payload)
    if not silent:
        _print_response(resp)
    return resp


def circuit_solver(base_url: str, start: str = "", *, silent: bool = False) -> dict[str, Any]:
    """Calculate a circuit starting at a node."""
    payload = {"start": start} if start else {}
    resp = request_json("POST", f"{base_url}/request/fulfillment-circuit", payload)
    if not silent:
        _print_response(resp)
    return resp


def get_results(base_url: str, *, silent: bool = False) -> dict[str, Any]:
    """Fetch all results from the server."""
    resp = request_json("GET", f"{base_url}/results")
    if not silent:
        _print_response(resp)
    return resp


def generate_map(
    base_url: str,
    *,
    nodes: int = 10,
    density: float = 0.3,
    active_prob: float = 0.9,
    secure_prob: float = 0.9,
    output: str | None = None,
    upload: bool = False,
    silent: bool = False,
) -> dict[str, Any]:
    """Generate a random map JSON and optionally upload it."""
    node_list = build_generated_map(
        nodes=nodes,
        density=density,
        active_prob=active_prob,
        secure_prob=secure_prob,
    )

    output_json = json.dumps(node_list, indent=4, ensure_ascii=False)

    if output:
        Path(output).write_text(output_json, encoding="utf-8")
        if not silent:
            print(f"  Map with {nodes} nodes saved to {output}")
    elif not silent:
        print(output_json)

    if upload:
        if not silent:
            print(f"\n  Uploading to {base_url}/map...")
        resp = request_json("POST", f"{base_url}/map", node_list)
        if not silent:
            _print_response(resp)
        return resp

    return {"status": "ok", "map": node_list}

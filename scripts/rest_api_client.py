#!/usr/bin/env python3
"""
Minimal client for the REST API exposed by the Dockerized server.

Examples:
  python scripts/rest_api_client.py upload-map --file json_format.json
"""

from __future__ import annotations

import argparse
import json
import random
import sys
import urllib.error
import urllib.parse
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

    request = urllib.request.Request(url=url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
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


def command_upload_map(args: argparse.Namespace) -> int:
    try:
        payload = load_json_file(args.file)
    except Exception as exc:
        print(f"Error reading {args.file}: {exc}", file=sys.stderr)
        return 1

    response = request_json("POST", f"{args.base_url}/map", payload)
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0


def command_flow_capacity(args: argparse.Namespace) -> int:
    payload = {
        "source": args.source,
        "sink": args.sink
    }
    response = request_json("POST", f"{args.base_url}/request/fulfillment-flow", payload)
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0

def command_circuit_solver(args: argparse.Namespace) -> int:
    payload = {
        "start": args.start
    }
    response = request_json("POST", f"{args.base_url}/request/fulfillment-circuit", payload)
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0


def command_generate_map(args: argparse.Namespace) -> int:
    nodes = []
    node_types = ["fulfillment_center", "market"]
    tags_pool = ["food", "ammo", "medical", "fuel", "tools"]
    conn_types = ["road", "rail", "trail", "tunnel", "bridge", "waterway", "drone", "blocked", "manual"]
    cond_types = ["infected_activity", "weather_rain", "foggy_visibility", "cleared", "reinforced"]

    num_nodes = args.nodes
    density = args.density
    active_prob = args.active_prob
    secure_prob = args.secure_prob

    for i in range(num_nodes):
        node_id = f"N{i:03d}"
        node_tags = random.sample(tags_pool, k=random.randint(0, len(tags_pool)))
        is_active = random.random() < active_prob
        is_secure = random.random() < secure_prob
        
        node = {
            "node_id": node_id,
            "node_name": f"Generated Node {i}",
            # "node_type": random.choice(node_types),
            "node_type": "fulfillment_center",
            "node_description": "Randomly generated testing node",
            "node_location": {
                "latitude": round(random.uniform(-90.0, 90.0), 4),
                "longitude": round(random.uniform(-180.0, 180.0), 4)
            },
            "node_tags": node_tags,
            "is_secure": is_secure,
            "is_active": is_active,
            "connections": [],
            "schema": {
                "version": 1
            }
        }
        nodes.append(node)

    for i, node in enumerate(nodes):
        for j in range(num_nodes):
            if i != j and random.random() < density:
                dest_id = f"N{j:03d}"
                conds = random.sample(cond_types, k=random.randint(0, len(cond_types)))
                edge = {
                    "to": dest_id,
                    "connection_type": random.choice(conn_types),
                    "base_weight": round(random.uniform(1.0, 100.0), 2),
                    "connection_conditions": conds
                }
                node["connections"].append(edge)

    output_json = json.dumps(nodes, indent=4, ensure_ascii=False)
    
    if args.output:
        Path(args.output).write_text(output_json, encoding="utf-8")
        print(f"Map with {num_nodes} nodes saved to {args.output}")
    else:
        print(output_json)

    if args.upload:
        print(f"\nUploading to {args.base_url}/map...")
        response = request_json("POST", f"{args.base_url}/map", nodes)
        print(json.dumps(response, indent=2, ensure_ascii=False))

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Client for the LoRa-CHADS REST API")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL, help="REST API base URL")

    subparsers = parser.add_subparsers(dest="command", required=True)

    upload_map = subparsers.add_parser("upload-map", help="Upload a map to /map")
    upload_map.add_argument("--file", required=True, help="Path to a JSON file containing the map data array")
    upload_map.set_defaults(func=command_upload_map)

    flow_capacity = subparsers.add_parser("flow-capacity", help="Calculate max flow between source and sink")
    flow_capacity.add_argument("--source", required=True, help="Source node ID")
    flow_capacity.add_argument("--sink", required=True, help="Sink node ID")
    flow_capacity.set_defaults(func=command_flow_capacity)

    circuit_solver = subparsers.add_parser("circuit-solver", help="Calculate a circuit starting at a node")
    circuit_solver.add_argument("--start", required=True, help="Starting node ID")
    circuit_solver.set_defaults(func=command_circuit_solver)

    generate_map = subparsers.add_parser("generate-map", help="Generate a random map JSON")
    generate_map.add_argument("--nodes", type=int, default=10, help="Number of nodes to generate")
    generate_map.add_argument("--density", type=float, default=0.3, help="Probability of an edge between any two nodes")
    generate_map.add_argument("--active-prob", type=float, default=0.9, help="Probability a node is active")
    generate_map.add_argument("--secure-prob", type=float, default=0.9, help="Probability a node is secure")
    generate_map.add_argument("--output", help="File to save the generated JSON")
    generate_map.add_argument("--upload", action="store_true", help="Also upload the generated map to the server")
    generate_map.set_defaults(func=command_generate_map)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except ValueError as exc:
        parser.error(str(exc))
    except urllib.error.URLError as exc:
        print(f"REST API request failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
#!/usr/bin/env python3
"""
Minimal client for the REST API exposed by the Dockerized server.

Examples:
  python scripts/rest_api_client.py algorithms
  python scripts/rest_api_client.py upload-map --nodes 4 --matrix 0,1,1,0,1,0,1,1,1,1,0,1,0,1,1,0
  python scripts/rest_api_client.py upload-map --file /tmp/map.json
  python scripts/rest_api_client.py circuit --map-id map_2026-03-22T12:00:00Z_1
  python scripts/rest_api_client.py circuit --nodes 4 --matrix 0,1,1,0,1,0,1,1,1,1,0,1,0,1,1,0
  python scripts/rest_api_client.py results
  python scripts/rest_api_client.py results --algorithm fulfillment-circuit
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_BASE_URL = "http://127.0.0.1:8080"


def parse_matrix(matrix_text: str) -> list[int]:
    values = []
    for token in matrix_text.split(","):
        token = token.strip()
        if not token:
            continue
        values.append(int(token))
    return values


def load_json_file(file_path: str) -> dict[str, Any]:
    return json.loads(Path(file_path).read_text(encoding="utf-8"))


def request_json(method: str, url: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(url=url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
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


def build_map_payload(args: argparse.Namespace) -> dict[str, Any]:
    if args.file:
        payload = load_json_file(args.file)
    else:
        if args.nodes is None or args.matrix is None:
            raise ValueError("--nodes and --matrix are required when --file is not used")
        payload = {
            "nodes": args.nodes,
            "adjacency_matrix": parse_matrix(args.matrix),
        }
    return payload


def command_algorithms(args: argparse.Namespace) -> int:
    response = request_json("POST", f"{args.base_url}/request")
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0


def command_upload_map(args: argparse.Namespace) -> int:
    payload = build_map_payload(args)
    response = request_json("POST", f"{args.base_url}/map", payload)
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0


def command_circuit(args: argparse.Namespace) -> int:
    if args.map_id:
        payload = {"map_id": args.map_id}
    else:
        payload = build_map_payload(args)
    response = request_json("POST", f"{args.base_url}/fulfillment-circuit", payload)
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0


def command_flow(args: argparse.Namespace) -> int:
    if args.map_id:
        payload = {"map_id": args.map_id}
    else:
        payload = build_map_payload(args)
    response = request_json("POST", f"{args.base_url}/fulfillment-flow", payload)
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0


def command_results(args: argparse.Namespace) -> int:
    params = {}
    if args.result_id:
        params["result_id"] = args.result_id
    if args.algorithm:
        params["algorithm"] = args.algorithm

    query = urllib.parse.urlencode(params)
    url = f"{args.base_url}/results"
    if query:
        url = f"{url}?{query}"

    response = request_json("GET", url)
    print(json.dumps(response, indent=2, ensure_ascii=False))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Client for the LoRa-CHADS REST API")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL, help="REST API base URL")

    subparsers = parser.add_subparsers(dest="command", required=True)

    algorithms = subparsers.add_parser("algorithms", help="List available algorithms")
    algorithms.set_defaults(func=command_algorithms)

    upload_map = subparsers.add_parser("upload-map", help="Upload a map to /map")
    upload_map.add_argument("--nodes", type=int, help="Number of nodes in the adjacency matrix")
    upload_map.add_argument("--matrix", help="Flat row-major adjacency matrix, comma-separated")
    upload_map.add_argument("--file", help="Path to a JSON file with nodes + adjacency_matrix")
    upload_map.set_defaults(func=command_upload_map)

    circuit = subparsers.add_parser("circuit", help="Call /fulfillment-circuit")
    circuit.add_argument("--map-id", help="Previously uploaded map id")
    circuit.add_argument("--nodes", type=int, help="Number of nodes for inline execution")
    circuit.add_argument("--matrix", help="Flat row-major adjacency matrix, comma-separated")
    circuit.add_argument("--file", help="Path to a JSON file with nodes + adjacency_matrix")
    circuit.set_defaults(func=command_circuit)

    flow = subparsers.add_parser("flow", help="Call /fulfillment-flow")
    flow.add_argument("--map-id", help="Previously uploaded map id")
    flow.add_argument("--nodes", type=int, help="Number of nodes for inline execution")
    flow.add_argument("--matrix", help="Flat row-major adjacency matrix, comma-separated")
    flow.add_argument("--file", help="Path to a JSON file with nodes + adjacency_matrix")
    flow.set_defaults(func=command_flow)

    results = subparsers.add_parser("results", help="Fetch stored results")
    results.add_argument("--result-id", help="Filter by result id")
    results.add_argument("--algorithm", help="Filter by algorithm name")
    results.set_defaults(func=command_results)

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
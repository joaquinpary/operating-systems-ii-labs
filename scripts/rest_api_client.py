#!/usr/bin/env python3
"""
Minimal client for the REST API exposed by the Dockerized server.

Examples:
  python scripts/rest_api_client.py upload-map --file json_format.json
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
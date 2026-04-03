#!/usr/bin/env python3
"""Load-test the Go API gateway using only the Python standard library.

Examples:
    python3 scripts/load_test_gateway.py --clients 1200 --concurrency 300 --requests-per-client 2 --mode mixed
    python3 scripts/load_test_gateway.py --clients 1000 --concurrency 1000 --requests-per-client 1 --mode shipments
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import http.client
import json
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from typing import Any
from urllib.parse import urlparse


DEFAULT_BASE_URL = "http://127.0.0.1:8080"
DEFAULT_JWT_SECRET = "change-me"
EXPECTED_STATUS = {
	"shipments": {202},
	"dispatch": {202},
	"status": {200},
}


@dataclass(slots=True)
class GatewayTarget:
	scheme: str
	host: str
	port: int
	base_path: str

	def build_path(self, endpoint: str) -> str:
		if self.base_path:
			return f"{self.base_path}{endpoint}"
		return endpoint


@dataclass(slots=True)
class RequestSpec:
	operation: str
	method: str
	path: str
	body: bytes | None
	headers: dict[str, str]


@dataclass(slots=True)
class WorkerResult:
	total_requests: int = 0
	successful_responses: int = 0
	unexpected_responses: int = 0
	transport_errors: int = 0
	status_counts: Counter = field(default_factory=Counter)
	unexpected_status_counts: Counter = field(default_factory=Counter)
	error_counts: Counter = field(default_factory=Counter)
	operation_counts: Counter = field(default_factory=Counter)
	latencies_ms: list[float] = field(default_factory=list)

	def merge(self, other: "WorkerResult") -> None:
		self.total_requests += other.total_requests
		self.successful_responses += other.successful_responses
		self.unexpected_responses += other.unexpected_responses
		self.transport_errors += other.transport_errors
		self.status_counts.update(other.status_counts)
		self.unexpected_status_counts.update(other.unexpected_status_counts)
		self.error_counts.update(other.error_counts)
		self.operation_counts.update(other.operation_counts)
		self.latencies_ms.extend(other.latencies_ms)


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Load-test the LoRa-CHADS API gateway")
	parser.add_argument("--base-url", default=DEFAULT_BASE_URL, help="Gateway base URL")
	parser.add_argument("--jwt-secret", default=DEFAULT_JWT_SECRET, help="JWT secret used by the gateway")
	parser.add_argument(
		"--mode",
		choices=("shipments", "dispatch", "status", "mixed"),
		default="mixed",
		help="Operation type to test",
	)
	parser.add_argument("--clients", type=int, default=1000, help="Number of virtual clients")
	parser.add_argument(
		"--concurrency",
		type=int,
		default=200,
		help="Maximum clients active at the same time",
	)
	parser.add_argument(
		"--requests-per-client",
		type=int,
		default=1,
		help="Requests executed sequentially by each virtual client",
	)
	parser.add_argument("--timeout", type=float, default=5.0, help="Per-request timeout in seconds")
	parser.add_argument("--output", default="", help="Optional JSON file to store the summary")
	return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
	if args.clients <= 0:
		raise SystemExit("--clients must be greater than 0")

	if args.concurrency <= 0:
		raise SystemExit("--concurrency must be greater than 0")

	if args.requests_per_client <= 0:
		raise SystemExit("--requests-per-client must be greater than 0")


def parse_target(base_url: str) -> GatewayTarget:
	parsed = urlparse(base_url)
	if parsed.scheme not in {"http", "https"}:
		raise SystemExit("base URL must start with http:// or https://")

	if not parsed.hostname:
		raise SystemExit("base URL must include a host")

	port = parsed.port
	if port is None:
		port = 443 if parsed.scheme == "https" else 80

	base_path = parsed.path.rstrip("/")
	return GatewayTarget(
		scheme=parsed.scheme,
		host=parsed.hostname,
		port=port,
		base_path=base_path,
	)


def b64url(data: bytes) -> str:
	return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def build_jwt(secret: str, subject: str = "load-test") -> str:
	header = b64url(json.dumps({"alg": "HS256", "typ": "JWT"}, separators=(",", ":")).encode("utf-8"))
	payload = b64url(json.dumps({"sub": subject, "exp": 4102444800}, separators=(",", ":")).encode("utf-8"))
	signing_input = f"{header}.{payload}".encode("ascii")
	signature = b64url(hmac.new(secret.encode("utf-8"), signing_input, hashlib.sha256).digest())
	return f"{header}.{payload}.{signature}"


def choose_operation(mode: str, client_index: int, request_index: int) -> str:
	if mode != "mixed":
		return mode

	selector = (client_index * 31 + request_index * 17) % 10
	if selector < 5:
		return "status"
	if selector < 8:
		return "shipments"
	return "dispatch"


def build_request_spec(
	target: GatewayTarget,
	token: str,
	operation: str,
	client_index: int,
	request_index: int,
) -> RequestSpec:
	headers = {
		"Authorization": f"Bearer {token}",
		"Accept": "application/json",
	}

	shipment_id = f"shipment-{client_index:05d}-{request_index:03d}"
	if operation == "shipments":
		body = json.dumps(
			{
				"origin_id": f"hub-{client_index % 500:04d}",
				"items": [
					{"item_id": 1, "item_name": "food", "quantity": 3},
					{"item_id": 2, "item_name": "water", "quantity": 5},
				],
			},
			separators=(",", ":"),
		).encode("utf-8")
		headers["Content-Type"] = "application/json"
		return RequestSpec(
			operation=operation,
			method="POST",
			path=target.build_path("/shipments"),
			body=body,
			headers=headers,
		)

	if operation == "dispatch":
		body = json.dumps({"shipment_id": shipment_id}, separators=(",", ":")).encode("utf-8")
		headers["Content-Type"] = "application/json"
		return RequestSpec(
			operation=operation,
			method="POST",
			path=target.build_path("/dispatch"),
			body=body,
			headers=headers,
		)

	return RequestSpec(
		operation="status",
		method="GET",
		path=target.build_path(f"/status/{shipment_id}"),
		body=None,
		headers=headers,
	)


def new_connection(target: GatewayTarget, timeout: float) -> http.client.HTTPConnection:
	if target.scheme == "https":
		return http.client.HTTPSConnection(target.host, target.port, timeout=timeout)
	return http.client.HTTPConnection(target.host, target.port, timeout=timeout)


def run_virtual_client(
	target: GatewayTarget,
	token: str,
	mode: str,
	requests_per_client: int,
	timeout: float,
	client_index: int,
) -> WorkerResult:
	result = WorkerResult()
	connection = new_connection(target, timeout)

	for request_index in range(requests_per_client):
		operation = choose_operation(mode, client_index, request_index)
		spec = build_request_spec(target, token, operation, client_index, request_index)
		start = time.perf_counter()

		try:
			connection.request(spec.method, spec.path, body=spec.body, headers=spec.headers)
			response = connection.getresponse()
			response.read()
			status_code = response.status
			latency_ms = (time.perf_counter() - start) * 1000

			result.total_requests += 1
			result.operation_counts[spec.operation] += 1
			result.status_counts[status_code] += 1
			result.latencies_ms.append(latency_ms)

			if status_code in EXPECTED_STATUS[spec.operation]:
				result.successful_responses += 1
			else:
				result.unexpected_responses += 1
				result.unexpected_status_counts[f"{spec.operation}:{status_code}"] += 1
		except Exception as exc:  # noqa: BLE001
			latency_ms = (time.perf_counter() - start) * 1000
			result.total_requests += 1
			result.operation_counts[spec.operation] += 1
			result.transport_errors += 1
			result.latencies_ms.append(latency_ms)
			result.error_counts[f"{type(exc).__name__}: {exc}"] += 1

			try:
				connection.close()
			except Exception:  # noqa: BLE001
				pass
			connection = new_connection(target, timeout)

	try:
		connection.close()
	except Exception:  # noqa: BLE001
		pass

	return result


def percentile(values: list[float], ratio: float) -> float:
	if not values:
		return 0.0

	sorted_values = sorted(values)
	index = int((len(sorted_values) - 1) * ratio)
	return sorted_values[index]


def build_summary(
	args: argparse.Namespace,
	target: GatewayTarget,
	result: WorkerResult,
	elapsed_seconds: float,
) -> dict[str, Any]:
	total_requests = result.total_requests
	requests_per_second = total_requests / elapsed_seconds if elapsed_seconds > 0 else 0.0

	return {
		"base_url": f"{target.scheme}://{target.host}:{target.port}{target.base_path}",
		"mode": args.mode,
		"clients": args.clients,
		"concurrency": min(args.concurrency, args.clients),
		"requests_per_client": args.requests_per_client,
		"total_requests": total_requests,
		"successful_responses": result.successful_responses,
		"unexpected_responses": result.unexpected_responses,
		"transport_errors": result.transport_errors,
		"elapsed_seconds": round(elapsed_seconds, 3),
		"requests_per_second": round(requests_per_second, 2),
		"latency_ms": {
			"p50": round(percentile(result.latencies_ms, 0.50), 2),
			"p95": round(percentile(result.latencies_ms, 0.95), 2),
			"p99": round(percentile(result.latencies_ms, 0.99), 2),
			"max": round(max(result.latencies_ms) if result.latencies_ms else 0.0, 2),
		},
		"operation_counts": dict(sorted(result.operation_counts.items())),
		"status_counts": dict(sorted(result.status_counts.items())),
		"unexpected_status_counts": dict(sorted(result.unexpected_status_counts.items())),
		"top_transport_errors": dict(result.error_counts.most_common(10)),
	}


def print_summary(summary: dict[str, Any]) -> None:
	print()
	print("Gateway Load Test Summary")
	print("=" * 72)
	print(f"Base URL:            {summary['base_url']}")
	print(f"Mode:                {summary['mode']}")
	print(f"Clients:             {summary['clients']}")
	print(f"Concurrency:         {summary['concurrency']}")
	print(f"Requests per client: {summary['requests_per_client']}")
	print(f"Total requests:      {summary['total_requests']}")
	print(f"Expected responses:  {summary['successful_responses']}")
	print(f"Unexpected HTTP:     {summary['unexpected_responses']}")
	print(f"Transport errors:    {summary['transport_errors']}")
	print(f"Elapsed seconds:     {summary['elapsed_seconds']}")
	print(f"Requests/second:     {summary['requests_per_second']}")
	print()
	print("Latency (ms)")
	print(f"  p50: {summary['latency_ms']['p50']}")
	print(f"  p95: {summary['latency_ms']['p95']}")
	print(f"  p99: {summary['latency_ms']['p99']}")
	print(f"  max: {summary['latency_ms']['max']}")
	print()
	print("Operation counts")
	for operation, count in summary["operation_counts"].items():
		print(f"  {operation}: {count}")
	print()
	print("HTTP status counts")
	for status_code, count in summary["status_counts"].items():
		print(f"  {status_code}: {count}")
	if summary["unexpected_status_counts"]:
		print()
		print("Unexpected status by operation")
		for label, count in summary["unexpected_status_counts"].items():
			print(f"  {label}: {count}")
	if summary["top_transport_errors"]:
		print()
		print("Top transport errors")
		for message, count in summary["top_transport_errors"].items():
			print(f"  {count}x {message}")
	print("=" * 72)


def save_summary(path: str, summary: dict[str, Any]) -> None:
	with open(path, "w", encoding="utf-8") as output_file:
		json.dump(summary, output_file, indent=2)
		output_file.write("\n")


def main() -> int:
	args = parse_args()
	validate_args(args)
	target = parse_target(args.base_url)
	token = build_jwt(args.jwt_secret)
	worker_count = min(args.concurrency, args.clients)
	aggregated = WorkerResult()

	start = time.perf_counter()
	with ThreadPoolExecutor(max_workers=worker_count) as executor:
		futures = [
			executor.submit(
				run_virtual_client,
				target,
				token,
				args.mode,
				args.requests_per_client,
				args.timeout,
				client_index,
			)
			for client_index in range(args.clients)
		]

		for future in as_completed(futures):
			aggregated.merge(future.result())

	elapsed_seconds = time.perf_counter() - start
	summary = build_summary(args, target, aggregated, elapsed_seconds)
	print_summary(summary)

	if args.output:
		save_summary(args.output, summary)
		print(f"Summary written to {args.output}")

	if summary["unexpected_responses"] > 0 or summary["transport_errors"] > 0:
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
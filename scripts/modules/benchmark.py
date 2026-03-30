"""Benchmark helpers for fulfillment-flow and fulfillment-circuit profiling."""

from __future__ import annotations

import json
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

from modules import rest_api_client

FLOW_ALGORITHM = "fulfillment-flow"
CIRCUIT_ALGORITHM = "fulfillment-circuit"
MAX_CIRCUIT_NODES = 20
BENCHMARK_DIR = Path(__file__).resolve().parents[2] / "logs" / "benchmarks"


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _format_iso(dt: datetime) -> str:
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


def _compact_stamp() -> str:
    return _utc_now().strftime("%Y%m%dT%H%M%S%f")[:-3] + "Z"


def _ensure_benchmark_dir() -> None:
    BENCHMARK_DIR.mkdir(parents=True, exist_ok=True)


def _default_output_path(prefix: str) -> Path:
    _ensure_benchmark_dir()
    return BENCHMARK_DIR / f"{prefix}_{_compact_stamp()}.json"


def _print_row(record: dict[str, Any], extra: str = "") -> None:
    print(
        "  "
        f"iter={record['iteration']:>2} "
        f"nodes={record['node_count']:>4} "
        f"time={record['execution_time_ms']:>10.3f} ms "
        f"openmp={str(record['use_openmp']).lower():>5}"
        f"{extra}"
    )


def _extract_timestamp(result: dict[str, Any]) -> str | None:
    timestamp = result.get("timestamp")
    if isinstance(timestamp, str) and timestamp:
        return timestamp

    timestamp_ms = result.get("timestamp_ms")
    if isinstance(timestamp_ms, (int, float)):
        dt = datetime.fromtimestamp(float(timestamp_ms) / 1000.0, tz=timezone.utc)
        return _format_iso(dt)

    return None


def _extract_execution_time_ms(result: dict[str, Any]) -> float | None:
    execution_time_ms = result.get("execution_time_ms")
    if isinstance(execution_time_ms, (int, float)):
        return float(execution_time_ms)

    nested_result = result.get("result")
    if isinstance(nested_result, dict):
        execution_time_ms = nested_result.get("execution_time_ms")
        if isinstance(execution_time_ms, (int, float)):
            return float(execution_time_ms)

    return None


def _extract_node_count(result: dict[str, Any]) -> int | None:
    node_count = result.get("node_count")
    if isinstance(node_count, (int, float)):
        return int(node_count)

    nested_result = result.get("result")
    if isinstance(nested_result, dict):
        node_count = nested_result.get("node_count")
        if isinstance(node_count, (int, float)):
            return int(node_count)

    request = result.get("request")
    if isinstance(request, dict):
        subgraph_node_ids = request.get("subgraph_node_ids")
        if isinstance(subgraph_node_ids, list):
            return len(subgraph_node_ids)

    return None


def normalize_result_document(result: dict[str, Any]) -> dict[str, Any] | None:
    algorithm = result.get("algorithm")
    if not isinstance(algorithm, str):
        return None

    timestamp = _extract_timestamp(result)
    execution_time_ms = _extract_execution_time_ms(result)
    node_count = _extract_node_count(result)

    if timestamp is None or execution_time_ms is None or node_count is None:
        return None

    use_openmp = result.get("use_openmp")
    if isinstance(use_openmp, bool):
        normalized_use_openmp = use_openmp
    elif use_openmp is None:
        normalized_use_openmp = False
    else:
        normalized_use_openmp = bool(use_openmp)

    normalized = {
        "algorithm": algorithm,
        "timestamp": timestamp,
        "execution_time_ms": execution_time_ms,
        "node_count": node_count,
        "use_openmp": normalized_use_openmp,
    }

    request = result.get("request")
    if isinstance(request, dict):
        for key in ("source", "sink", "start"):
            value = request.get(key)
            if isinstance(value, str):
                normalized[key] = value

    return normalized


def normalize_result_documents(results: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    normalized: list[dict[str, Any]] = []
    for result in results:
        if not isinstance(result, dict):
            continue
        normalized_result = normalize_result_document(result)
        if normalized_result is not None:
            normalized.append(normalized_result)
    return normalized


def _filter_results_by_timestamps(
    results: Iterable[dict[str, Any]],
    *,
    algorithm: str,
    timestamps: set[str],
    use_openmp_values: set[bool],
) -> list[dict[str, Any]]:
    filtered: list[dict[str, Any]] = []
    for result in results:
        if not isinstance(result, dict):
            continue
        if result.get("algorithm") != algorithm:
            continue

        timestamp = _extract_timestamp(result)
        if timestamp not in timestamps:
            continue

        use_openmp = result.get("use_openmp")
        normalized_use_openmp = bool(use_openmp) if use_openmp is not None else False
        if normalized_use_openmp not in use_openmp_values:
            continue

        filtered.append(result)

    return filtered


def _print_summary(records: list[dict[str, Any]], *, title: str) -> None:
    if not records:
        print(f"\n  {title}: no records collected")
        return

    grouped: dict[int, list[float]] = defaultdict(list)
    for record in records:
        grouped[record["node_count"]].append(record["execution_time_ms"])

    print(f"\n  {title}")
    for node_count in sorted(grouped):
        values = grouped[node_count]
        average = sum(values) / len(values)
        print(
            "  "
            f"nodes={node_count:>4} "
            f"avg={average:>10.3f} ms "
            f"samples={len(values):>2}"
        )


def _build_flow_node_counts(max_nodes: int, step: int) -> list[int]:
    node_counts = list(range(10, max_nodes + 1, step))
    if node_counts and node_counts[-1] != max_nodes:
        node_counts.append(max_nodes)
    return node_counts


def _write_benchmark_output(
    *,
    prefix: str,
    payload: dict[str, Any],
    output_path: str | None,
) -> str:
    destination = Path(output_path) if output_path else _default_output_path(prefix)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    return str(destination)


def run_flow_benchmark(
    base_url: str,
    *,
    max_nodes: int = 1000,
    step: int = 10,
    iterations: int = 5,
    density: float = 0.3,
    output_path: str | None = None,
) -> dict[str, Any]:
    if max_nodes < 10:
        return {"status": "error", "message": "Flow benchmark requires max_nodes >= 10"}
    if step <= 0:
        return {"status": "error", "message": "Flow benchmark requires step > 0"}
    if iterations <= 0:
        return {"status": "error", "message": "Flow benchmark requires iterations > 0"}

    started_at = _format_iso(_utc_now())
    records: list[dict[str, Any]] = []
    timestamps: set[str] = set()
    use_openmp_values: set[bool] = set()

    print("\n  Flow benchmark")
    for iteration in range(1, iterations + 1):
        for node_count in _build_flow_node_counts(max_nodes, step):
            map_response = rest_api_client.generate_map(
                base_url,
                nodes=node_count,
                density=density,
                active_prob=1.0,
                secure_prob=1.0,
                upload=True,
                silent=True,
            )
            if map_response.get("status") != "ok":
                return {
                    "status": "error",
                    "message": f"Map upload failed for {node_count} nodes",
                    "response": map_response,
                    "records": records,
                }

            sink = f"N{node_count - 1:03d}"
            response = rest_api_client.flow_capacity(base_url, "N000", sink, silent=True)
            if response.get("status") != "ok":
                return {
                    "status": "error",
                    "message": f"Flow request failed for {node_count} nodes",
                    "response": response,
                    "records": records,
                }

            timestamp = response.get("timestamp")
            execution_time_ms = float(response.get("execution_time_ms", 0.0))
            use_openmp = bool(response.get("use_openmp", False))
            record = {
                "algorithm": FLOW_ALGORITHM,
                "iteration": iteration,
                "node_count": int(response.get("node_count", node_count)),
                "execution_time_ms": execution_time_ms,
                "use_openmp": use_openmp,
                "timestamp": timestamp,
                "source": "N000",
                "sink": sink,
                "max_flow": float(response.get("max_flow", 0.0)),
            }
            records.append(record)
            if isinstance(timestamp, str):
                timestamps.add(timestamp)
            use_openmp_values.add(use_openmp)
            _print_row(record, extra=f" max_flow={record['max_flow']:.3f}")

    results_response = rest_api_client.get_results(base_url, silent=True)
    raw_results = results_response.get("results", []) if isinstance(results_response, dict) else []
    filtered_results = _filter_results_by_timestamps(
        raw_results,
        algorithm=FLOW_ALGORITHM,
        timestamps=timestamps,
        use_openmp_values=use_openmp_values,
    )
    normalized_results = normalize_result_documents(filtered_results)
    finished_at = _format_iso(_utc_now())

    payload = {
        "status": "ok",
        "algorithm": FLOW_ALGORITHM,
        "base_url": base_url,
        "started_at": started_at,
        "finished_at": finished_at,
        "iterations": iterations,
        "max_nodes": max_nodes,
        "step": step,
        "density": density,
        "records": records,
        "result_documents": filtered_results,
        "normalized_results": normalized_results,
    }
    saved_path = _write_benchmark_output(prefix="flow_benchmark", payload=payload, output_path=output_path)
    payload["output_path"] = saved_path

    _print_summary(records, title="Flow summary")
    print(f"\n  Flow benchmark data saved to {saved_path}")
    return payload


def run_circuit_benchmark(
    base_url: str,
    *,
    max_nodes: int = 15,
    iterations: int = 5,
    density: float = 0.5,
    output_path: str | None = None,
) -> dict[str, Any]:
    if max_nodes < 2:
        return {"status": "error", "message": "Circuit benchmark requires max_nodes >= 2"}
    if iterations <= 0:
        return {"status": "error", "message": "Circuit benchmark requires iterations > 0"}

    bounded_max_nodes = min(max_nodes, MAX_CIRCUIT_NODES)
    if bounded_max_nodes != max_nodes:
        print(f"\n  Circuit max nodes capped to {MAX_CIRCUIT_NODES} by server limit")
    started_at = _format_iso(_utc_now())
    records: list[dict[str, Any]] = []
    timestamps: set[str] = set()
    use_openmp_values: set[bool] = set()

    print("\n  Circuit benchmark")
    for iteration in range(1, iterations + 1):
        for node_count in range(2, bounded_max_nodes + 1):
            map_response = rest_api_client.generate_map(
                base_url,
                nodes=node_count,
                density=density,
                active_prob=1.0,
                secure_prob=1.0,
                upload=True,
                silent=True,
            )
            if map_response.get("status") != "ok":
                return {
                    "status": "error",
                    "message": f"Map upload failed for {node_count} nodes",
                    "response": map_response,
                    "records": records,
                }

            response = rest_api_client.circuit_solver(base_url, "N000", silent=True)
            if response.get("status") != "ok":
                return {
                    "status": "error",
                    "message": f"Circuit request failed for {node_count} nodes",
                    "response": response,
                    "records": records,
                }

            timestamp = response.get("timestamp")
            execution_time_ms = float(response.get("execution_time_ms", 0.0))
            use_openmp = bool(response.get("use_openmp", False))
            circuits = response.get("circuits", [])
            record = {
                "algorithm": CIRCUIT_ALGORITHM,
                "iteration": iteration,
                "node_count": int(response.get("node_count", node_count)),
                "execution_time_ms": execution_time_ms,
                "use_openmp": use_openmp,
                "timestamp": timestamp,
                "start": "N000",
                "has_circuit": bool(response.get("has_circuit", False)),
                "circuit_count": len(circuits) if isinstance(circuits, list) else 0,
            }
            records.append(record)
            if isinstance(timestamp, str):
                timestamps.add(timestamp)
            use_openmp_values.add(use_openmp)
            _print_row(record, extra=f" circuits={record['circuit_count']:>2}")

    results_response = rest_api_client.get_results(base_url, silent=True)
    raw_results = results_response.get("results", []) if isinstance(results_response, dict) else []
    filtered_results = _filter_results_by_timestamps(
        raw_results,
        algorithm=CIRCUIT_ALGORITHM,
        timestamps=timestamps,
        use_openmp_values=use_openmp_values,
    )
    normalized_results = normalize_result_documents(filtered_results)
    finished_at = _format_iso(_utc_now())

    payload = {
        "status": "ok",
        "algorithm": CIRCUIT_ALGORITHM,
        "base_url": base_url,
        "started_at": started_at,
        "finished_at": finished_at,
        "iterations": iterations,
        "max_nodes": bounded_max_nodes,
        "density": density,
        "records": records,
        "result_documents": filtered_results,
        "normalized_results": normalized_results,
    }
    saved_path = _write_benchmark_output(prefix="circuit_benchmark", payload=payload, output_path=output_path)
    payload["output_path"] = saved_path

    _print_summary(records, title="Circuit summary")
    print(f"\n  Circuit benchmark data saved to {saved_path}")
    return payload
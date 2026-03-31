"""Plot profiling comparisons for benchmarked server results."""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

from modules import benchmark, rest_api_client

PLOT_DIR = Path(__file__).resolve().parents[2] / "logs" / "benchmarks"


def _compact_stamp() -> str:
    return benchmark._compact_stamp()


def _default_output_path() -> Path:
    PLOT_DIR.mkdir(parents=True, exist_ok=True)
    return PLOT_DIR / f"profiling_{_compact_stamp()}.png"


def _load_records_from_json(file_path: str) -> list[dict[str, Any]]:
    payload = json.loads(Path(file_path).read_text(encoding="utf-8"))

    if isinstance(payload, list):
        if payload and isinstance(payload[0], dict) and "algorithm" in payload[0] and "execution_time_ms" in payload[0]:
            return [item for item in payload if isinstance(item, dict)]
        return []

    if not isinstance(payload, dict):
        return []

    if isinstance(payload.get("normalized_results"), list):
        return [item for item in payload["normalized_results"] if isinstance(item, dict)]

    if isinstance(payload.get("records"), list):
        return [item for item in payload["records"] if isinstance(item, dict)]

    if isinstance(payload.get("result_documents"), list):
        return benchmark.normalize_result_documents(payload["result_documents"])

    if isinstance(payload.get("results"), list):
        return benchmark.normalize_result_documents(payload["results"])

    return []


def _aggregate_records(records: Iterable[dict[str, Any]]) -> dict[tuple[str, bool], dict[int, list[float]]]:
    aggregated: dict[tuple[str, bool], dict[int, list[float]]] = defaultdict(lambda: defaultdict(list))

    for record in records:
        algorithm = record.get("algorithm")
        node_count = record.get("node_count")
        execution_time_ms = record.get("execution_time_ms")
        use_openmp = record.get("use_openmp", False)
        if not isinstance(algorithm, str):
            continue
        if not isinstance(node_count, (int, float)):
            continue
        if not isinstance(execution_time_ms, (int, float)):
            continue

        aggregated[(algorithm, bool(use_openmp))][int(node_count)].append(float(execution_time_ms))

    return aggregated


def plot_records(records: list[dict[str, Any]], *, output_path: str | None = None) -> dict[str, Any]:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return {
            "status": "error",
            "message": "matplotlib is required to generate profiling graphs. Install it with: pip install matplotlib",
        }

    aggregated = _aggregate_records(records)
    if not aggregated:
        return {"status": "error", "message": "No profiling records available to plot"}

    series_styles = {
        (benchmark.FLOW_ALGORITHM, True): {
            "label": "Flow + OpenMP",
            "color": "#0f766e",
            "linestyle": "-",
            "marker": "o",
        },
        (benchmark.FLOW_ALGORITHM, False): {
            "label": "Flow - OpenMP",
            "color": "#115e59",
            "linestyle": "--",
            "marker": "o",
        },
        (benchmark.CIRCUIT_ALGORITHM, True): {
            "label": "Circuit + OpenMP",
            "color": "#b45309",
            "linestyle": "-",
            "marker": "s",
        },
        (benchmark.CIRCUIT_ALGORITHM, False): {
            "label": "Circuit - OpenMP",
            "color": "#92400e",
            "linestyle": "--",
            "marker": "s",
        },
    }

    fig, ax = plt.subplots(figsize=(10, 6))
    plotted_series = 0
    total_points = 0

    for key, style in series_styles.items():
        node_groups = aggregated.get(key)
        if not node_groups:
            continue

        x_values = sorted(node_groups)
        y_values = [sum(node_groups[node_count]) / len(node_groups[node_count]) for node_count in x_values]
        total_points += len(x_values)
        ax.plot(
            x_values,
            y_values,
            label=style["label"],
            color=style["color"],
            linestyle=style["linestyle"],
            marker=style["marker"],
            linewidth=2,
            markersize=6,
        )
        plotted_series += 1

    if plotted_series == 0:
        plt.close(fig)
        return {"status": "error", "message": "No compatible profiling series were found"}

    ax.set_title("Algorithm profiling comparison")
    ax.set_xlabel("Node count")
    ax.set_ylabel("Average execution time (ms)")
    ax.grid(True, linestyle=":", linewidth=0.8, alpha=0.7)
    ax.legend()
    fig.tight_layout()

    destination = Path(output_path) if output_path else _default_output_path()
    destination.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(destination, dpi=150, bbox_inches="tight")
    plt.close(fig)

    print(f"\n  Profiling graph saved to {destination}")
    return {
        "status": "ok",
        "output_path": str(destination),
        "series_count": plotted_series,
        "point_count": total_points,
    }


def plot_from_server(base_url: str, *, output_path: str | None = None) -> dict[str, Any]:
    response = rest_api_client.get_results(base_url, silent=True)
    if response.get("status") == "error":
        return response
    results = response.get("results", []) if isinstance(response, dict) else []
    records = benchmark.normalize_result_documents(results)
    return plot_records(records, output_path=output_path)


def plot_from_file(file_path: str, *, output_path: str | None = None) -> dict[str, Any]:
    try:
        records = _load_records_from_json(file_path)
    except FileNotFoundError:
        return {"status": "error", "message": f"File not found: {file_path}"}
    except json.JSONDecodeError as exc:
        return {"status": "error", "message": f"Invalid JSON file: {exc}"}
    return plot_records(records, output_path=output_path)
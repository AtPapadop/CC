#!/usr/bin/env python3
"""Plot CC benchmark results aggregated from CSV files."""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import matplotlib.pyplot as plt

plt.style.use("seaborn-v0_8-colorblind")

_THREAD_HEADER_RE = re.compile(r"^(?P<count>\d+)\s*Thread[s]?$", re.IGNORECASE)


def _mean(values: Iterable[float]) -> float:
    data = list(values)
    if not data:
        raise ValueError("Cannot compute mean of empty dataset")
    return sum(data) / len(data)


def _parse_csv(path: Path) -> Tuple[str, str, Dict[int, float], float | None]:
    """Return matrix, algorithm, per-thread means, and optional sequential mean."""
    matrix: str
    algorithm: str
    name = path.stem  # results_<algorithm>_<matrix>
    parts = name.split("_")
    if len(parts) < 3 or parts[0] != "results":
        raise ValueError(f"Unexpected file naming pattern: {path.name}")
    matrix = parts[-1]
    algorithm = "_".join(parts[1:-1])

    with path.open(newline="") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None:
            raise ValueError(f"CSV file has no header: {path}")
        columns = [col.strip() for col in reader.fieldnames if col]
        raw_rows = list(reader)

    per_thread: Dict[int, List[float]] = defaultdict(list)
    sequential_values: List[float] = []

    for row in raw_rows:
        for column in columns:
            cell = row.get(column, "").strip()
            if not cell:
                continue
            try:
                value = float(cell)
            except ValueError as exc:  # pragma: no cover - guarding against bad data
                raise ValueError(f"Non-numeric value in {path.name} column '{column}': {cell}") from exc
            match = _THREAD_HEADER_RE.match(column)
            if match:
                per_thread[int(match.group("count"))].append(value)
            else:
                sequential_values.append(value)

    per_thread_means: Dict[int, float] = {count: _mean(vals) for count, vals in per_thread.items()}
    sequential_mean = _mean(sequential_values) if sequential_values else None
    return matrix, algorithm, per_thread_means, sequential_mean


def _gather_results(folder: Path) -> Tuple[str, Dict[str, Dict[int, float]], Dict[str, float]]:
    matrix_name: str | None = None
    thread_data: Dict[str, Dict[int, float]] = {}
    sequential_data: Dict[str, float] = {}

    csv_paths = sorted(folder.glob("results_*_*.csv"))
    if not csv_paths:
        raise FileNotFoundError(f"No results_<_>_<_>.csv files found in {folder}")

    for csv_path in csv_paths:
        matrix, algorithm, per_thread, sequential_mean = _parse_csv(csv_path)
        if matrix_name is None:
            matrix_name = matrix
        elif matrix_name != matrix:
            raise ValueError(f"Mixed matrix names detected: {matrix_name} and {matrix} (from {csv_path.name})")
        if per_thread:
            thread_data[algorithm] = per_thread
        if sequential_mean is not None:
            sequential_data[algorithm] = sequential_mean

    if matrix_name is None:
        raise RuntimeError("Failed to determine matrix name from CSV files")

    return matrix_name, thread_data, sequential_data


def _build_series(
    thread_data: Dict[str, Dict[int, float]], sequential_data: Dict[str, float]
) -> Tuple[List[int], Dict[str, List[float]]]:
    all_counts = sorted({count for data in thread_data.values() for count in data})
    if not all_counts:
        all_counts = [1]

    series: Dict[str, List[float]] = {}
    for algorithm, data in thread_data.items():
        series[algorithm] = [data.get(count) for count in all_counts]

    for algorithm, seq_mean in sequential_data.items():
        if algorithm in series:
            continue
        series[algorithm] = [seq_mean for _ in all_counts]

    return all_counts, series


def plot_results(folder: Path, output: Path | None = None) -> Path:
    matrix_name, thread_data, sequential_data = _gather_results(folder)
    thread_counts, series = _build_series(thread_data, sequential_data)

    if not series:
        raise RuntimeError("No series data to plot")

    fig, ax = plt.subplots(figsize=(10, 6), dpi=150)
    for algorithm, values in series.items():
        ax.plot(thread_counts, values, marker="o", linewidth=2, markersize=6, label=algorithm)

    ax.set_xlabel("Threads", fontsize=12)
    ax.set_ylabel("Average runtime", fontsize=12)
    ax.set_title(f"Connected Components on {matrix_name}", fontsize=14, fontweight="bold")
    ax.set_xticks(thread_counts)
    ax.tick_params(labelsize=10)
    ax.legend(frameon=False)
    ax.grid(True, linestyle=":", linewidth=0.6, alpha=0.7)

    if output is None:
        output = folder / f"results_plot_{matrix_name}.png"

    fig.tight_layout()
    fig.savefig(output, dpi=300, bbox_inches="tight", facecolor="white")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot CC benchmark results from CSV files")
    parser.add_argument("folder", type=Path, help="Directory containing results_<_>_<_>.csv files")
    parser.add_argument("--output", type=Path, help="Optional output PNG path")
    args = parser.parse_args()

    output_path = plot_results(args.folder, args.output)
    print(f"Plot saved to {output_path}")


if __name__ == "__main__":
    main()

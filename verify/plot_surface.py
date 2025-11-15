#!/usr/bin/env python3
"""Render a 3D surface plot (threads × chunk size → avg runtime) from sweep CSV output."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib.pyplot as plt
import numpy as np
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 - needed for 3D projection

plt.style.use("seaborn-v0_8-colorblind")

_REQUIRED_COLUMNS = {"threads", "chunk_size", "average_seconds"}


def _infer_matrix_name(path: Path) -> str:
    stem = path.stem
    parts = stem.split("_")
    if len(parts) >= 4 and parts[0] == "results" and parts[1] == "pthread" and parts[2] == "surface":
        return "_".join(parts[3:])
    return stem


def _load_surface_data(csv_path: Path) -> Tuple[List[int], List[int], np.ndarray]:
    with csv_path.open(newline="") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None:
            raise ValueError(f"CSV file {csv_path} is missing a header row")
        header = {field.strip() for field in reader.fieldnames if field}
        if not _REQUIRED_COLUMNS.issubset(header):
            missing = ", ".join(sorted(_REQUIRED_COLUMNS - header))
            raise ValueError(f"CSV file {csv_path} is missing required columns: {missing}")

        values: Dict[Tuple[int, int], float] = {}
        threads: set[int] = set()
        chunks: set[int] = set()
        for row in reader:
            try:
                thread = int(row["threads"])  # type: ignore[arg-type]
                chunk = int(row["chunk_size"])  # type: ignore[arg-type]
                avg = float(row["average_seconds"])  # type: ignore[arg-type]
            except (TypeError, ValueError) as exc:
                raise ValueError(f"Invalid numeric value in row: {row}") from exc
            key = (thread, chunk)
            values[key] = avg
            threads.add(thread)
            chunks.add(chunk)

    if not values:
        raise ValueError(f"CSV file {csv_path} contains no data rows")

    thread_list = sorted(threads)
    chunk_list = sorted(chunks)
    surface = np.empty((len(thread_list), len(chunk_list)), dtype=float)

    for ti, thread in enumerate(thread_list):
        for ci, chunk in enumerate(chunk_list):
            try:
                surface[ti, ci] = values[(thread, chunk)]
            except KeyError as exc:
                raise ValueError(
                    f"Missing value for thread={thread}, chunk_size={chunk}. "
                    "Ensure the sweep ran every combination."
                ) from exc

    return thread_list, chunk_list, surface


def plot_surface(csv_path: Path, output: Path | None, elev: float, azim: float, cmap: str) -> Path:
    threads, chunks, surface = _load_surface_data(csv_path)
    matrix_name = _infer_matrix_name(csv_path)

    X, Y = np.meshgrid(chunks, threads)
    Z = surface

    fig = plt.figure(figsize=(10, 7), dpi=150)
    ax = fig.add_subplot(111, projection="3d")

    surf = ax.plot_surface(X, Y, Z, cmap=cmap, edgecolor="k", linewidth=0.3, antialiased=True, alpha=0.9)
    fig.colorbar(surf, shrink=0.6, aspect=12, pad=0.1, label="Average seconds")

    ax.set_xlabel("Chunk size")
    ax.set_ylabel("Threads")
    ax.set_zlabel("Average seconds")
    ax.set_title(f"Pthreads CC Surface on {matrix_name}", fontweight="bold")
    ax.view_init(elev=elev, azim=azim)

    ax.set_xticks(chunks)
    ax.set_yticks(threads)

    if output is None:
        output = csv_path.with_name(f"surface_plot_{matrix_name}.png")

    fig.tight_layout()
    fig.savefig(output, dpi=300, bbox_inches="tight", facecolor="white")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot a 3D surface from pthread sweep CSV data")
    parser.add_argument("csv", type=Path, help="Path to results_pthread_surface_<matrix>.csv")
    parser.add_argument("--output", type=Path, help="Optional output PNG path")
    parser.add_argument("--elev", type=float, default=30.0, help="Elevation angle in degrees (default 30)")
    parser.add_argument("--azim", type=float, default=-60.0, help="Azimuth angle in degrees (default -60)")
    parser.add_argument(
        "--cmap",
        type=str,
        default="viridis",
        help="Matplotlib colormap for the surface (default 'viridis')",
    )
    args = parser.parse_args()

    output = plot_surface(args.csv, args.output, args.elev, args.azim, args.cmap)
    print(f"Surface plot saved to {output}")


if __name__ == "__main__":
    main()

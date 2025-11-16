#!/usr/bin/env python3
"""Render a 3D surface plot (threads × chunk size → avg runtime) from sweep CSV output.
Also generates a 2D projection plot onto the chunk-size axis (chunk_size → runtime).
"""

from __future__ import annotations

import argparse
from asyncio import threads
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
                thread = int(row["threads"])
                chunk = int(row["chunk_size"])
                avg = float(row["average_seconds"])
            except (TypeError, ValueError) as exc:
                raise ValueError(f"Invalid numeric value in row: {row}") from exc

            if chunk == 1:
                continue

            values[(thread, chunk)] = avg
            threads.add(thread)
            chunks.add(chunk)

    if not values:
        raise ValueError(f"CSV file {csv_path} contains no usable data")

    thread_list = sorted(threads)
    chunk_list = sorted(chunks)
    surface = np.empty((len(thread_list), len(chunk_list)), dtype=float)

    for ti, thread in enumerate(thread_list):
        for ci, chunk in enumerate(chunk_list):
            try:
                surface[ti, ci] = values[(thread, chunk)]
            except KeyError as exc:
                raise ValueError(f"Missing value for thread={thread}, chunk={chunk}") from exc

    return thread_list, chunk_list, surface


def plot_surface_and_projection(csv_path: Path, output: Path | None, elev: float, azim: float, cmap: str) -> Tuple[Path, Path]:
    threads, chunks, surface = _load_surface_data(csv_path)
    matrix_name = _infer_matrix_name(csv_path)

    # LOG2-transformed X axis
    chunks_arr = np.array(chunks, dtype=float)
    X_log = np.log2(chunks_arr)
    X, Y = np.meshgrid(X_log, threads)
    Z = surface

    # ============================
    #  1. 3D SURFACE PLOT
    # ============================
    fig = plt.figure(figsize=(10, 7), dpi=150)
    ax = fig.add_subplot(111, projection="3d")

    surf = ax.plot_surface(
        X, Y, Z,
        cmap=cmap,
        edgecolor="k",
        linewidth=0.3,
        antialiased=True,
        alpha=0.9
    )
    fig.colorbar(surf, shrink=0.6, aspect=12, pad=0.1, label="Average seconds")

    xtick_positions = np.log2(chunks_arr)
    xtick_labels = [f"2^{int(np.log2(c))}" if np.log2(c).is_integer() else str(c) for c in chunks_arr]

    ax.set_xticks(xtick_positions)
    ax.set_xticklabels(xtick_labels)
    ax.set_xlabel("Chunk size (log₂)")

    ax.set_yticks(threads)
    ax.set_ylabel("Threads")
    ax.set_zlabel("Average seconds")

    ax.set_title(f"Pthreads CC Surface on {matrix_name}", fontweight="bold")
    ax.view_init(elev=elev, azim=azim)

    if output is None:
        output_surface = csv_path.with_name(f"surface_plot_{matrix_name}.png")
    else:
        output_surface = output

    fig.tight_layout()
    fig.savefig(output_surface, dpi=300, bbox_inches="tight", facecolor="white")
    plt.close(fig)

    # ============================
    #  2. 2D PROJECTION PLOT
    # ============================
    fig2 = plt.figure(figsize=(9, 6), dpi=150)
    ax2 = fig2.add_subplot(111)

    # --- Distinct qualitative colormap for clear lines ---
    cmap_proj = plt.get_cmap("tab20")
    # You can also use:
    #   "Dark2", "Set1", "Set2", "tab10", etc.

    # Plot each thread as a distinct colored line
    for i, t in enumerate(threads):
        if t == 1:       # <-- skip 1-thread line
            continue

        color = cmap_proj(i % 20)
        ax2.plot(
            X_log,
            surface[i, :],
            label=f"{t} threads",
            color=color,
            linewidth=2,
            marker="o",
            markersize=4
        )

    ax2.set_xlabel("Chunk size (log₂)")
    ax2.set_ylabel("Average seconds")
    ax2.set_title(f"Runtime vs Chunk Size Projection ({matrix_name})")

    ax2.set_xticks(xtick_positions)
    ax2.set_xticklabels(xtick_labels)

    ax2.grid(True, alpha=0.3)
    ax2.legend(title="Threads", bbox_to_anchor=(1.04, 1), loc="upper left")

    output_projection = csv_path.with_name(f"projection_plot_{matrix_name}.png")

    fig2.tight_layout()
    fig2.savefig(output_projection, dpi=300, bbox_inches="tight", facecolor="white")
    plt.close(fig2)

    return output_surface, output_projection


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot a 3D surface and a 2D projection from pthread sweep CSV data")
    parser.add_argument("csv", type=Path, help="Path to results_pthread_surface_<matrix>.csv")
    parser.add_argument("--output", type=Path, help="Optional output PNG path for 3D surface")
    parser.add_argument("--elev", type=float, default=30.0)
    parser.add_argument("--azim", type=float, default=-60.0)
    parser.add_argument("--cmap", type=str, default="viridis")

    args = parser.parse_args()

    surface_png, proj_png = plot_surface_and_projection(
        args.csv, args.output, args.elev, args.azim, args.cmap
    )

    print(f"Surface plot saved to: {surface_png}")
    print(f"Projection plot saved to: {proj_png}")


if __name__ == "__main__":
    main()

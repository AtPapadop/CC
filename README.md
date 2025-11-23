# Connected Components Benchmarks

This repository provides multiple connected-components executables spanning sequential, OpenMP, Pthreads, and OpenCilk implementations plus helper scripts for verification and visualization.

## Requirements
- C compiler with C11 support (e.g. `gcc`, `clang`)
- Compiler toolchain with OpenCilk support. Set CILK_CC accordingly in the `Makefile` or your environment as fitting, for example:
  ```bash
  export CILK_CC=/usr/local/opencilk/bin/clang
  ```
- Make utility
- libmatio (for .mat file support). On Ubuntu, install via:
  ```bash
  sudo apt-get install libmatio-dev
  ```

## Building

```bash
make
make <target>
```

Available targets include `cc`, `cc_omp`, `cc_pthreads`, `cc_cilk`, and `cc_pthreads_sweep`.

Artifacts are written to `bin/` and depend on the common graph/CC utilities under `src/`.

## Executables

All executables can be invoked with `--help` to see usage details.

### `bin/cc`
- **What**: Sequential label propagation (`--algorithm lp`) or BFS (`--algorithm bfs`).
- **Why**: Acts as the correctness baseline and produces `c_labels.txt`/`bfs_labels.txt` plus CSV timing columns (`results_c_*` / `results_bfs_*`).
- **Usage**:
	```bash
	bin/cc --algorithm bfs --runs 5 --output results data/com-LiveJournal.mtx
	```

### `bin/cc_omp`
- **What**: OpenMP label propagation with optional sweeps over thread counts (`-t/--threads` accepts comma lists and `start:end[:step]`).
- **Outputs**: `omp_labels.txt` and extra columns inside `results_omp_<matrix>.csv` per thread count.
- **Usage**:
	```bash
	bin/cc_omp --threads 1:16:2 --runs 3 --chunk-size 2048 data/com-LiveJournal.mtx
	```
- **Chunking tip**: Passing `--chunk-size 1` disables dynamic chunking and reverts to a static OpenMP schedule (one block per thread).

### `bin/cc_pthreads`
- **What**: Pthreads implementation for multiple thread counts. Great for apples-to-apples comparisons with OpenMP.
- **Usage**:
	```bash
	bin/cc_pthreads --threads 8,9:13 --runs 5 --chunk-size 4096 data/com-LiveJournal.mtx
	```
- **Chunking tip**: `--chunk-size 1` disables the dynamic work queue, letting each thread process a contiguous static block.

### `bin/cc_cilk`
- **What**: OpenCilk version driven by `CILK_NWORKERS`. Supports `--runs`, `--chunk-size`, and `--output`.
- **Usage**:
	```bash
	CILK_NWORKERS=12 bin/cc_cilk --runs 3 --chunk-size 2048 data/com-LiveJournal.mtx
	```
- **Chunking tip**: As with the other kernels, `--chunk-size 1` means "no chunking" for Cilk as well, matching the runtime’s default static distribution.

### `bin/cc_pthreads_sweep`
- **What**: Parameter sweep that records `(threads, chunk_size, average_seconds)` for Pthreads so you can build 3D performance surfaces.
- **Usage**:
	```bash
	bin/cc_pthreads_sweep --threads 1:16:2 --chunk-size 512,1024,2048,4096 --runs 100 data/com-LiveJournal.mtx
	```
- **Output**: `results/results_pthread_surface_<matrix>.csv` with schema
	```
	threads,chunk_size,average_seconds
	1,1024,0.456123
	1,2048,0.432987
	...
	```
	Perfect input for `verify/plot_surface.py` (see below).

## Verification & plotting tools

All helper scripts live in `verify/` and can be invoked directly (ensure Python deps such as `matplotlib`, `numpy`, `networkx`, `scipy` are installed).

| Script | Purpose |
| --- | --- |
| `verify/test.sh` | Bash harness that runs bfs, OpenMP, OpenCilk, and Pthreads binaries over a range of threads/workers, capturing results in a chosen folder. |
| `verify/cc_verify.py` | Loads a Matrix Market graph with SciPy/NetworkX, computes connected components in Python, prints the component count, and emits `python_labels.txt` for cross-checking. |
| `verify/plot_results.py` | Aggregates the traditional `results_*.csv` files (sequential/OpenMP/Cilk/etc.) and produces 2D runtime vs. threads plots. |
| `verify/plot_surface.py` | Reads the `results_pthread_surface_<matrix>.csv` sweep output and renders a 3D surface plot (threads × chunk size → average runtime) as well as a 2D projection plot onto the chunk-size axis. |
| `verify/cc_benchmark.m` | MATLAB benchmarking helper mirroring the C pipeline for comparison to MATLAB's built-in `conncomp` function. |

Example surface plot command:

```bash
python3 verify/plot_surface.py results/com-LiveJournal/pthreads/results_pthread_surface_com-LiveJournal.csv \
		--output results/com-LiveJournal/pthreads/surface.png --elev 35 --azim -45
```

For standard 2D plots:

```bash
python3 verify/plot_results.py results/com-LiveJournal/
```

Both plotting scripts default to sensible styling (Seaborn colorblind palette) and will emit the PNG path on success.

**Note:** Most Python scripts depend on `matplotlib`, `numpy`, and `scipy`. A quick way to set them up is:
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install matplotlib numpy scipy networkx
```

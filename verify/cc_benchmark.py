#!/usr/bin/env python3
import argparse, os, sys, time
import numpy as np
import scipy.io
import scipy.sparse as sp

def load_mtx(path):
    A = scipy.io.mmread(path)
    if not sp.isspmatrix(A):
        A = sp.csr_matrix(A)
    A = A.tocsr()
    # symmetrize cheaply (boolean OR)
    A = (A + A.T).astype(bool)
    A.setdiag(False)
    A.eliminate_zeros()
    return A.astype(np.int8)

def load_mat73_uf(path):
    # UF v7.3 .mat: /Problem/A/{ir,jc,data}
    import h5py
    with h5py.File(path, "r") as f:
        ir   = np.array(f["/Problem/A/ir"],   dtype=np.int64)
        jc   = np.array(f["/Problem/A/jc"],   dtype=np.int64)
        data = np.array(f["/Problem/A/data"])
    n = int(ir.max() + 1)
    m = int(jc.size - 1)
    csc = sp.csc_matrix((data, ir, jc), shape=(n, m))
    A = csc.tocsr()
    # symmetrize cheaply (boolean OR)
    A = (A + A.T).astype(bool)
    A.setdiag(False)
    A.eliminate_zeros()
    return A.astype(np.int8)

def load_graph(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".mtx":
        return load_mtx(path)
    elif ext == ".mat":
        return load_mat73_uf(path)
    else:
        raise ValueError(f"Unsupported file type: {ext}")

def edges_from_upper(A):
    # take only upper triangle to avoid duplicate undirected edges
    coo = sp.triu(A, k=1, format="coo")
    return coo.row.astype(np.int64), coo.col.astype(np.int64)

def run_graphtool(A, threads, runs):
    import graph_tool.all as gt
    gt.openmp_set_num_threads(threads)
    r, c = edges_from_upper(A)
    g = gt.Graph(directed=False)
    g.add_edge_list(zip(r, c))
    times = []
    labels = None
    for i in range(runs):
        t0 = time.perf_counter()
        comp, hist = gt.label_components(g)
        t1 = time.perf_counter()
        times.append(t1 - t0)
        print(f"Run {i+1}/{runs}: {t1 - t0:.6f} s, components={len(hist)}")
        labels = comp.a.copy()
    return labels, float(np.mean(times))

def run_igraph(A, threads, runs):
    import igraph as ig
    # igraph uses OpenMP; control with environment vars
    os.environ["OMP_NUM_THREADS"] = str(threads)
    os.environ["OPENBLAS_NUM_THREADS"] = str(threads)
    os.environ["MKL_NUM_THREADS"] = str(threads)
    r, c = edges_from_upper(A)
    n = A.shape[0]
    g = ig.Graph(n=n, edges=list(zip(map(int, r), map(int, c))), directed=False)
    times = []
    labels = None
    for i in range(runs):
        t0 = time.perf_counter()
        comps = g.connected_components()
        t1 = time.perf_counter()
        times.append(t1 - t0)
        print(f"Run {i+1}/{runs}: {t1 - t0:.6f} s, components={len(comps)}")
        lab = np.empty(n, dtype=np.int64)
        for idx, verts in enumerate(comps):
            lab[np.fromiter(verts, dtype=np.int64)] = idx
        labels = lab
    return labels, float(np.mean(times))

def run_scipy(A, threads, runs):
    # fallback (serial)
    from scipy.sparse.csgraph import connected_components
    os.environ["OMP_NUM_THREADS"] = str(threads)
    os.environ["OPENBLAS_NUM_THREADS"] = str(threads)
    os.environ["MKL_NUM_THREADS"] = str(threads)
    times = []
    labels = None
    for i in range(runs):
        t0 = time.perf_counter()
        ncomp, lab = connected_components(A, directed=False)
        t1 = time.perf_counter()
        times.append(t1 - t0)
        print(f"Run {i+1}/{runs}: {t1 - t0:.6f} s, components={ncomp}")
        labels = lab.astype(np.int64)
    return labels, float(np.mean(times))

def main():
    p = argparse.ArgumentParser(description="Parallel CC benchmark (graph-tool / igraph / SciPy fallback)")
    p.add_argument("threads", type=int)
    p.add_argument("runs", type=int)
    p.add_argument("file", type=str)
    args = p.parse_args()

    print(f"Loading graph from {args.file} ...")
    A = load_graph(args.file)
    n = A.shape[0]; m = A.nnz // 2
    print(f"Graph: {n} nodes, {m} undirected edges")

    # Try graph-tool (fastest), then igraph, then SciPy
    labels = None; avg = None
    tried = []
    try:
        import graph_tool.all  # noqa
        tried.append("graph-tool")
        labels, avg = run_graphtool(A, args.threads, args.runs)
        backend = "graph-tool"
    except Exception as e_gt:
        print(f"[info] graph-tool not available or failed: {e_gt}")
        try:
            import igraph  # noqa
            tried.append("igraph")
            labels, avg = run_igraph(A, args.threads, args.runs)
            backend = "igraph"
        except Exception as e_ig:
            print(f"[info] igraph not available or failed: {e_ig}")
            tried.append("scipy (serial)")
            labels, avg = run_scipy(A, args.threads, args.runs)
            backend = "scipy (serial)"

    np.savetxt("python_labels.txt", labels, fmt="%d")
    print("Labels saved to python_labels.txt")
    print(f"Backend: {backend} (tried: {', '.join(tried)})")
    print(f"Average time: {avg:.6f} s with threads={args.threads}")

if __name__ == "__main__":
    main()

#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200112L

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "graph.h"
#include "cc.h"

/*
 * Afforest-style connected components with pthreads.
 *
 * High-level algorithm:
 *   1. parent[v] = v for all v
 *   2. Sampling phase (1 round):
 *        For each vertex u, if it has at least one neighbor v:
 *          UNION(u, first_neighbor(u))
 *   3. Compress parents (pointer jumping)
 *   4. Full edge phase:
 *        For each undirected edge (u, v), with v > u:
 *          UNION(u, v)
 *   5. Compress parents again
 *   6. labels[v] = parent root of v
 *
 * This uses a parallel union-find with atomic CAS-based hooking.
 * Graph is assumed undirected (neighbors appear in both directions).
 */

typedef struct
{
  const CSRGraph *G;
  atomic_int *parent;          // union-find parent array
  int32_t n;

  int thread_id;
  int num_threads;

  pthread_barrier_t *barrier;
} ThreadArgs;

/* Helper: compute static vertex range [start, end) for thread t */
static inline void thread_vertex_range(int32_t n, int t, int num_threads,
                                       int32_t *start, int32_t *end)
{
  int64_t block = ((int64_t)n + num_threads - 1) / num_threads;
  int64_t s = (int64_t)t * block;
  int64_t e = s + block;
  if (s > n) s = n;
  if (e > n) e = n;
  *start = (int32_t)s;
  *end = (int32_t)e;
}

/* Find with path splitting / pointer jumping */
static inline int32_t uf_find(atomic_int *restrict parent, int32_t x)
{
  int32_t p = atomic_load_explicit(&parent[x], memory_order_relaxed);
  while (p != x)
  {
    int32_t pp = atomic_load_explicit(&parent[p], memory_order_relaxed);
    if (pp != p)
    {
      // Path splitting: x -> grandparent
      atomic_store_explicit(&parent[x], pp, memory_order_relaxed);
    }
    x = p;
    p = pp;
  }
  return x;
}

/* Parallel-safe union by hooking larger root under smaller root */
static inline void uf_union(atomic_int *restrict parent, int32_t x, int32_t y)
{
  while (1)
  {
    int32_t rx = uf_find(parent, x);
    int32_t ry = uf_find(parent, y);

    if (rx == ry)
      return;

    // Always hook larger id under smaller id to avoid cycles
    if (rx < ry)
    {
      int32_t tmp = rx;
      rx = ry;
      ry = tmp;
    }
    // Now rx > ry

    int32_t prx = atomic_load_explicit(&parent[rx], memory_order_relaxed);
    if (prx != rx)
    {
      // rx is no longer a root, retry
      continue;
    }

    // Try to hook rx under ry
    if (atomic_compare_exchange_strong_explicit(&parent[rx],
                                                &prx,
                                                ry,
                                                memory_order_relaxed,
                                                memory_order_relaxed))
    {
      return; // success
    }
    // CAS failed, retry
  }
}

/* Compress parents for all vertices in [start, end) */
static inline void uf_compress_range(atomic_int *restrict parent,
                                     int32_t start,
                                     int32_t end)
{
  for (int32_t i = start; i < end; i++)
  {
    int32_t root = uf_find(parent, i);
    atomic_store_explicit(&parent[i], root, memory_order_relaxed);
  }
}

/* Worker thread for Afforest-style CC */
static void *afforest_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs *)arg;
  const CSRGraph *G = args->G;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;
  atomic_int *restrict parent = args->parent;
  const int32_t n = args->n;

  pthread_barrier_t *barrier = args->barrier;
  const int thread_id = args->thread_id;
  const int num_threads = args->num_threads;

  int32_t v_start, v_end;
  thread_vertex_range(n, thread_id, num_threads, &v_start, &v_end);

  /* ---- Phase 1: Sampling (one round) ----
   * For each vertex u in this thread's range:
   *   If it has neighbors, union(u, first_neighbor(u)).
   */
  pthread_barrier_wait(barrier);

  for (int32_t u = v_start; u < v_end; u++)
  {
    int64_t begin = row_ptr[u];
    int64_t end = row_ptr[u + 1];
    if (begin < end)
    {
      int32_t v = col_idx[begin]; // first neighbor
      if (v >= 0 && v < n)
        uf_union(parent, u, v);
    }
  }

  pthread_barrier_wait(barrier);

  /* ---- Phase 2: Compress parents after sampling ---- */
  uf_compress_range(parent, v_start, v_end);

  pthread_barrier_wait(barrier);

  /* ---- Phase 3: Full edge pass ----
   * Process all edges (u, v) with v > u to avoid duplicates.
   * Each thread handles vertices in [v_start, v_end).
   */
  for (int32_t u = v_start; u < v_end; u++)
  {
    int64_t begin = row_ptr[u];
    int64_t end = row_ptr[u + 1];
    for (int64_t j = begin; j < end; j++)
    {
      int32_t v = col_idx[j];
      if (v > u) // rely on undirected graph, avoid double work
      {
        uf_union(parent, u, v);
      }
    }
  }

  pthread_barrier_wait(barrier);

  /* ---- Phase 4: Final compression ---- */
  uf_compress_range(parent, v_start, v_end);

  pthread_barrier_wait(barrier);

  return NULL;
}

/*
 * Public entry point:
 *   Afforest-style CC using pthreads.
 *
 *   G          : CSR graph (undirected)
 *   labels     : output component labels (size n)
 *   num_threads: number of pthreads to use
 *   chunk_size : currently unused (kept for API compatibility)
 */
void compute_connected_components_pthreads_afforest(const CSRGraph *restrict G,
                                                    int32_t *restrict labels,
                                                    int num_threads,
                                                    int chunk_size)
{
  (void)chunk_size; // currently not used; placeholder for future tuning

  const int32_t n = G->n;
  if (n <= 0)
    return;

  if (num_threads <= 0)
    num_threads = 1;

  /* Allocate union-find parent array */
  atomic_int *parent;
  if (posix_memalign((void **)&parent, 64, (size_t)n * sizeof(atomic_int)) != 0)
  {
    fprintf(stderr, "Memory allocation failed (parent)\n");
    exit(EXIT_FAILURE);
  }

  /* Initialize parent[v] = v */
  for (int32_t i = 0; i < n; i++)
    atomic_init(&parent[i], i);

  /* Barrier and threads */
  pthread_barrier_t barrier;
  if (pthread_barrier_init(&barrier, NULL, (unsigned int)num_threads) != 0)
  {
    fprintf(stderr, "Failed to initialize pthread barrier\n");
    exit(EXIT_FAILURE);
  }

  pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
  ThreadArgs *args = malloc((size_t)num_threads * sizeof(ThreadArgs));
  if (!threads || !args)
  {
    fprintf(stderr, "Memory allocation failed (threads/args)\n");
    exit(EXIT_FAILURE);
  }

  for (int t = 0; t < num_threads; t++)
  {
    args[t].G = G;
    args[t].parent = parent;
    args[t].n = n;
    args[t].thread_id = t;
    args[t].num_threads = num_threads;
    args[t].barrier = &barrier;

    if (pthread_create(&threads[t], NULL, afforest_worker, &args[t]) != 0)
    {
      fprintf(stderr, "Failed to create thread %d\n", t);
      exit(EXIT_FAILURE);
    }
  }

  for (int t = 0; t < num_threads; t++)
    pthread_join(threads[t], NULL);

  /* Copy final roots into labels[] */
  for (int32_t i = 0; i < n; i++)
    labels[i] = atomic_load_explicit(&parent[i], memory_order_relaxed);

  /* Cleanup */
  pthread_barrier_destroy(&barrier);
  free(parent);
  free(threads);
  free(args);
}


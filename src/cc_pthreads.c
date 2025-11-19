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

// Thread arguments structure for pthreads
typedef struct
{
  const CSRGraph *G;
  atomic_int *labels;
  atomic_int *changed;
  atomic_int *next_vertex; // dynamic work index for chunk distribution
  int32_t n;
  int thread_id;
  int num_threads;
  pthread_barrier_t *barrier;
  int chunk_size;
  int chunking_enabled;
  int32_t block_start;
  int32_t block_end;
} ThreadArgs;

// Returns 1 when vertex u (or its neighbors) adopts a lower label
static inline int relax_vertex_label(int32_t u,
                                     const int64_t *restrict row_ptr,
                                     const int32_t *restrict col_idx,
                                     atomic_int *restrict labels)
{
  int32_t old_label = atomic_load_explicit(&labels[u], memory_order_relaxed);
  int32_t new_label = old_label;

  for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
  {
    int32_t v = col_idx[j];
    int32_t neighbor_label = atomic_load_explicit(&labels[v], memory_order_relaxed);
    if (neighbor_label < new_label)
      new_label = neighbor_label;
  }

  if (new_label < old_label)
  {
    int32_t current = old_label;
    while (current > new_label &&
           !atomic_compare_exchange_weak_explicit(&labels[u], &current, new_label,
                                                  memory_order_relaxed, memory_order_relaxed))
    {
    }

    for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
    {
      int32_t v = col_idx[j];
      int32_t neighbor = atomic_load_explicit(&labels[v], memory_order_relaxed);
      while (neighbor > new_label &&
             !atomic_compare_exchange_weak_explicit(&labels[v], &neighbor, new_label,
                                                    memory_order_relaxed, memory_order_relaxed))
      {
      }
    }

    return 1;
  }

  return 0;
}

// Worker thread: fully asynchronous label propagation
static void *lp_worker_full_async(void *arg)
{
  ThreadArgs *args = (ThreadArgs *)arg;
  const CSRGraph *G = args->G;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;
  const int32_t n = args->n;
  const int chunk = args->chunk_size;
  const int chunking_enabled = args->chunking_enabled;
  const int32_t block_start = args->block_start;
  const int32_t block_end = args->block_end;

  while (1)
  {
    int local_changed = 0;

    // Reset dynamic work queue at the start of the round
    if (chunking_enabled && args->thread_id == 0)
      atomic_store_explicit(args->next_vertex, 0, memory_order_relaxed);
    pthread_barrier_wait(args->barrier);

    if (chunking_enabled)
    {
      // Dynamic work distribution via atomic index in chunk_size blocks
      while (1)
      {
        int start = atomic_fetch_add_explicit(args->next_vertex, chunk, memory_order_relaxed);
        if (start >= n)
          break;
        int end = start + chunk;
        if (end > n)
          end = n;

        for (int32_t u = start; u < end; u++)
          local_changed |= relax_vertex_label(u, row_ptr, col_idx, args->labels);
      }
    }
    else
    {
      for (int32_t u = block_start; u < block_end; u++)
        local_changed |= relax_vertex_label(u, row_ptr, col_idx, args->labels);
    }

    // Mark if this thread changed anything
    if (local_changed)
      atomic_store_explicit(args->changed, 1, memory_order_relaxed);

    // Synchronize all threads
    pthread_barrier_wait(args->barrier);

    // One thread checks for convergence
    if (args->thread_id == 0)
    {
      if (atomic_load_explicit(args->changed, memory_order_acquire) == 0)
      {
        atomic_store_explicit(args->changed, -1, memory_order_release); // signal done
      }
      else
      {
        atomic_store_explicit(args->changed, 0, memory_order_relaxed); // reset flag
      }
    }

    pthread_barrier_wait(args->barrier);

    // Stop condition: changed == -1 means no thread changed anything
    if (atomic_load_explicit(args->changed, memory_order_acquire) == -1)
      break;
  }

  return NULL;
}

void compute_connected_components_pthreads(const CSRGraph *restrict G,
                                           int32_t *restrict labels,
                                           int num_threads,
                                           int chunk_size)
{
  const int32_t n = G->n;
  const int chunking_enabled = (chunk_size != 1);
  const int effective_chunk = (chunk_size > 0) ? chunk_size : DEFAULT_CHUNK_SIZE;
  const int32_t static_block = (!chunking_enabled && num_threads > 0)
                                ? (int32_t)(((int64_t)n + num_threads - 1) / num_threads) : 0;

  atomic_int *atomic_labels;
  if (posix_memalign((void **)&atomic_labels, 64, (size_t)n * sizeof(atomic_int)) != 0)
  {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  for (int32_t i = 0; i < n; i++)
    atomic_init(&atomic_labels[i], i);

  atomic_int changed;
  atomic_init(&changed, 1);

  // Dynamic work index for chunk-based scheduling
  atomic_int next_vertex;
  atomic_init(&next_vertex, 0);

  pthread_barrier_t barrier;
  pthread_barrier_init(&barrier, NULL, num_threads);

  pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
  ThreadArgs *args = malloc(num_threads * sizeof(ThreadArgs));

  for (int t = 0; t < num_threads; t++)
  {
    args[t].G = G;
    args[t].labels = atomic_labels;
    args[t].changed = &changed;
    args[t].next_vertex = &next_vertex;
    args[t].n = n;
    args[t].thread_id = t;
    args[t].num_threads = num_threads;
    args[t].barrier = &barrier;
    args[t].chunk_size = chunking_enabled ? effective_chunk : 0;
    args[t].chunking_enabled = chunking_enabled;
    if (!chunking_enabled)
    {
      int64_t start = (int64_t)t * static_block;
      int64_t end = start + static_block;
      if (start > n)
        start = n;
      if (end > n)
        end = n;
      args[t].block_start = (int32_t)start;
      args[t].block_end = (int32_t)end;
    }
    else
    {
      args[t].block_start = 0;
      args[t].block_end = 0;
    }
    pthread_create(&threads[t], NULL, lp_worker_full_async, &args[t]);
  }

  for (int t = 0; t < num_threads; t++)
    pthread_join(threads[t], NULL);

  // Copy results
  for (int32_t i = 0; i < n; i++)
    labels[i] = atomic_load_explicit(&atomic_labels[i], memory_order_relaxed);

  pthread_barrier_destroy(&barrier);
  free(atomic_labels);
  free(threads);
  free(args);
}

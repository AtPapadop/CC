#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200112L

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "graph.h"
#include "cc.h"

#define CHUNK_SIZE 4096

typedef struct
{
  const CSRGraph *restrict G;
  int32_t *restrict *restrict labels_ptr;     // shared pointers
  int32_t *restrict *restrict new_labels_ptr; // shared pointers
  uint8_t *restrict *restrict active_ptr;     // shared frontier pointers
  uint8_t *restrict *restrict next_active_ptr;
  atomic_int *restrict next_vertex;
  atomic_int *restrict changed;
  int *restrict done;
  pthread_barrier_t *restrict barrier;
  int thread_id;
  int num_threads;
} ThreadArgs;

/* -------------------------------------------------------------------------- */
/* Worker thread: label propagation with frontier optimization                */
/* -------------------------------------------------------------------------- */
static void *lp_worker_dynamic(void *arg)
{
  ThreadArgs *args = (ThreadArgs *)arg;
  const CSRGraph *restrict G = args->G;
  const int32_t n = G->n;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;

  while (1)
  {
    int local_changed = 0;
    int32_t *restrict labels = *args->labels_ptr;
    int32_t *restrict new_labels = *args->new_labels_ptr;
    uint8_t *restrict active = *args->active_ptr;
    uint8_t *restrict next_active = *args->next_active_ptr;

    // ---- Phase 1: Dynamic work distribution over ACTIVE vertices ----
    while (1)
    {
      int start = atomic_fetch_add(args->next_vertex, CHUNK_SIZE);
      if (start >= n)
        break;

      int end = (start + CHUNK_SIZE < n) ? start + CHUNK_SIZE : n;

      for (int32_t u = start; u < end; u++)
      {
        if (!active[u])
          continue; // skip inactive vertices

        int32_t old_label = labels[u];
        int32_t new_label = old_label;

        for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
        {
          int32_t v = col_idx[j];
          int32_t neighbor_label = labels[v];
          if (neighbor_label < new_label)
            new_label = neighbor_label;
        }

        new_labels[u] = new_label;
        if (new_label < old_label)
        {
          local_changed = 1;
          next_active[u] = 1;
          // Optionally activate neighbors to converge faster
          for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
            next_active[col_idx[j]] = 1;
        }
      }
    }

    if (local_changed)
      atomic_store(args->changed, 1);

    pthread_barrier_wait(args->barrier);

    // ---- Phase 2: Global synchronization + swap ----
    if (args->thread_id == 0)
    {
      if (atomic_load(args->changed) == 0)
      {
        *args->done = 1; // convergence reached
      }
      else
      {
        *args->done = 0;
        atomic_store(args->changed, 0);
        atomic_store(args->next_vertex, 0);

        // swap shared pointers so all threads see new arrays
        int32_t *tmp = *args->labels_ptr;
        *args->labels_ptr = *args->new_labels_ptr;
        *args->new_labels_ptr = tmp;

        // swap frontiers
        uint8_t *tmp_a = *args->active_ptr;
        *args->active_ptr = *args->next_active_ptr;
        *args->next_active_ptr = tmp_a;

        // clear new frontier for next iteration
        memset(*args->next_active_ptr, 0, n);
      }
    }

    pthread_barrier_wait(args->barrier);

    if (*args->done)
      break;
  }

  return NULL;
}

void compute_connected_components_pthreads(const CSRGraph *restrict G, int32_t *restrict labels, int num_threads)
{
  const int32_t n = G->n;

  // Initialize label arrays
  for (int32_t i = 0; i < n; i++)
    labels[i] = i;

  int32_t *restrict new_labels;
  if (posix_memalign((void **)&new_labels, 64, n * sizeof(int32_t)) != 0)
  {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  // Allocate frontier arrays
  uint8_t *restrict active;
  uint8_t *restrict next_active;
  if (posix_memalign((void **)&active, 64, n) != 0 ||
      posix_memalign((void **)&next_active, 64, n) != 0)
  {
    fprintf(stderr, "Memory allocation failed (active sets)\n");
    free(new_labels);
    exit(EXIT_FAILURE);
  }

  memset(active, 1, n); // initially all vertices active
  memset(next_active, 0, n);

  // Shared synchronization objects
  atomic_int next_vertex = 0;
  atomic_int changed = 1;
  int done = 0;

  pthread_barrier_t barrier;
  pthread_barrier_init(&barrier, NULL, num_threads);

  pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
  ThreadArgs *args = malloc(num_threads * sizeof(ThreadArgs));

  // shared pointers to allow swap visibility
  int32_t *restrict *restrict labels_ptr = &labels;
  int32_t *restrict *restrict new_labels_ptr = &new_labels;
  uint8_t *restrict *restrict active_ptr = &active;
  uint8_t *restrict *restrict next_active_ptr = &next_active;

  for (int t = 0; t < num_threads; t++)
  {
    args[t].G = G;
    args[t].labels_ptr = labels_ptr;
    args[t].new_labels_ptr = new_labels_ptr;
    args[t].active_ptr = active_ptr;
    args[t].next_active_ptr = next_active_ptr;
    args[t].next_vertex = &next_vertex;
    args[t].changed = &changed;
    args[t].done = &done;
    args[t].barrier = &barrier;
    args[t].thread_id = t;
    args[t].num_threads = num_threads;
  }

  // Spawn threads
  for (int t = 0; t < num_threads; t++)
    pthread_create(&threads[t], NULL, lp_worker_dynamic, &args[t]);

  for (int t = 0; t < num_threads; t++)
    pthread_join(threads[t], NULL);

  free(active);
  free(next_active);
  free(threads);
  free(args);
  pthread_barrier_destroy(&barrier);
}

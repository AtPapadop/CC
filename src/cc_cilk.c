#define _POSIX_C_SOURCE 200112L

#include "cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <cilk/cilk.h>

void compute_connected_components_cilk(const CSRGraph *restrict G,
                                       int32_t *restrict labels)
{
  const int32_t n = G->n;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;

  // --- Initialize labels ---
  for (int32_t i = 0; i < n; i++)
    labels[i] = i;

  // --- Allocate aligned arrays ---
  int32_t *aux_labels;
  if (posix_memalign((void **)&aux_labels, 64, n * sizeof(int32_t)) != 0)
  {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  int32_t *label_buffers[2] = {labels, aux_labels};
  int active_idx = 0;

  uint8_t *active, *next_active;
  if (posix_memalign((void **)&active, 64, n) != 0 ||
      posix_memalign((void **)&next_active, 64, n) != 0)
  {
    fprintf(stderr, "Memory allocation failed (frontier)\n");
    free(aux_labels);
    exit(EXIT_FAILURE);
  }

  memset(active, 1, n); // all vertices active initially
  memset(next_active, 0, n);

  while (1)
  {
    _Atomic int any_changed = 0;

    const int read_idx = active_idx;
    const int write_idx = read_idx ^ 1;
    const int32_t *restrict current_labels = label_buffers[read_idx];
    int32_t *restrict next_labels = label_buffers[write_idx];

    // Each worker keeps a local flag; only one atomic store per worker
    cilk_for(int32_t base = 0; base < n; base += 1024)
    {
      int local_changed = 0;
      int32_t end = (base + 1024 < n) ? base + 1024 : n;

      for (int32_t u = base; u < end; u++)
      {
        int32_t new_label = current_labels[u];

        if (active[u])
        {
          for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
          {
            int32_t v = col_idx[j];
            if (current_labels[v] < new_label)
              new_label = current_labels[v];
          }

          if (new_label < current_labels[u])
          {
            local_changed = 1;
            next_active[u] = 1;
            // optionally activate neighbors
            for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
              next_active[col_idx[j]] = 1;
          }
        }

        next_labels[u] = new_label;
      }

      if (local_changed)
        atomic_store_explicit(&any_changed, 1, memory_order_relaxed);
    }

    // --- Check for convergence ---
    if (atomic_load_explicit(&any_changed, memory_order_acquire) == 0)
      break;

    active_idx ^= 1;

    uint8_t *tmp_frontier = active;
    active = next_active;
    next_active = tmp_frontier;

    // --- Parallel frontier clearing ---
    cilk_for(int i = 0; i < n; i++)
      next_active[i] = 0;
  }

  if (active_idx != 0)
    memcpy(labels, label_buffers[active_idx], (size_t)n * sizeof(int32_t));

  free(aux_labels);
  free(active);
  free(next_active);
}

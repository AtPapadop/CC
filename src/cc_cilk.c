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
  int32_t *restrict new_labels;
  if (posix_memalign((void **)&new_labels, 64, n * sizeof(int32_t)) != 0)
  {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  uint8_t *restrict active, *restrict next_active;
  if (posix_memalign((void **)&active, 64, n) != 0 ||
      posix_memalign((void **)&next_active, 64, n) != 0)
  {
    fprintf(stderr, "Memory allocation failed (frontier)\n");
    free(new_labels);
    exit(EXIT_FAILURE);
  }

  memset(active, 1, n); // all vertices active initially
  memset(next_active, 0, n);

  int any_changed = 1;

  while (any_changed)
  {
    any_changed = 0;
    cilk_for(int32_t u = 0; u < n; u++)
    {
      if (!active[u])
        continue;

      int32_t old_label = labels[u];
      int32_t new_label = old_label;

      for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
      {
        int32_t v = col_idx[j];
        if (labels[v] < new_label)
          new_label = labels[v];
      }

      new_labels[u] = new_label;

      if (new_label < old_label)
      {
        next_active[u] = 1;
        // optionally activate neighbors
        for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
          next_active[col_idx[j]] = 1;
        __atomic_store_n(&any_changed, 1, __ATOMIC_RELAXED);
      }
    }

    // --- Swap arrays and frontiers ---
    int32_t *tmp_labels = labels;
    labels = new_labels;
    new_labels = tmp_labels;

    uint8_t *tmp_frontier = active;
    active = next_active;
    next_active = tmp_frontier;
    memset(next_active, 0, n);
  }

  free(new_labels);
  free(active);
  free(next_active);
}

#define _POSIX_C_SOURCE 200112L

#include "cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <cilk/cilk.h>

void compute_connected_components_cilk(const CSRGraph *restrict G, int32_t *restrict labels)
{
  const int32_t n = G->n;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;

  // --- Initialize labels ---
  for (int32_t i = 0; i < n; i++)
    labels[i] = i;

  _Atomic int32_t *atomic_labels;
  if (posix_memalign((void **)&atomic_labels, 64, (size_t)n * sizeof(*atomic_labels)) != 0)
  {
    fprintf(stderr, "Memory allocation failed (atomic labels)\n");
    exit(EXIT_FAILURE);
  }

  cilk_for(int32_t i = 0; i < n; i++)
    atomic_store_explicit(&atomic_labels[i], labels[i], memory_order_relaxed);

  while (1)
  {
    _Atomic int any_changed = 0;

    cilk_for(int32_t base = 0; base < n; base += 1024)
    {
      int local_changed = 0;
      int32_t end = (base + 1024 < n) ? base + 1024 : n;

      for (int32_t u = base; u < end; u++)
      {
        int32_t old_label = atomic_load_explicit(&atomic_labels[u], memory_order_relaxed);
        int32_t new_label = old_label;

        for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
        {
          int32_t v = col_idx[j];
          int32_t neighbor_label = atomic_load_explicit(&atomic_labels[v], memory_order_relaxed);
          if (neighbor_label < new_label)
            new_label = neighbor_label;
        }

        if (new_label < old_label)
        {
          int32_t current = old_label;
          while (current > new_label &&
                 !atomic_compare_exchange_weak_explicit(&atomic_labels[u],&current, new_label,
                                                        memory_order_relaxed,memory_order_relaxed))
          {
          }

          local_changed = 1;

          for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
          {
            int32_t v = col_idx[j];
            int32_t neighbor = atomic_load_explicit(&atomic_labels[v], memory_order_relaxed);
            while (neighbor > new_label &&
                   !atomic_compare_exchange_weak_explicit(&atomic_labels[v],&neighbor, new_label,
                                                          memory_order_relaxed,memory_order_relaxed))
            {
            }
          }
        }
      }

      if (local_changed)
        atomic_store_explicit(&any_changed, 1, memory_order_relaxed);
    }

    if (atomic_load_explicit(&any_changed, memory_order_acquire) == 0)
      break;
  }

  cilk_for(int32_t i = 0; i < n; i++)
    labels[i] = atomic_load_explicit(&atomic_labels[i], memory_order_relaxed);

  free(atomic_labels);
}

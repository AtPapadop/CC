#define _POSIX_C_SOURCE 200112L

#include "cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include <stdatomic.h>

void compute_connected_components_omp(const CSRGraph *restrict G,
                                      int32_t *restrict labels)
{
  const int32_t n = G->n;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;

// --- Initialize labels ---
#pragma omp parallel for schedule(static)
  for (int32_t i = 0; i < n; i++)
    labels[i] = i;

  // --- Aligned double buffers for labels ---
  int32_t *aux_labels;
  if (posix_memalign((void **)&aux_labels, 64, n * sizeof(int32_t)) != 0)
  {
    fprintf(stderr, "Memory allocation failed (labels)\n");
    exit(EXIT_FAILURE);
  }
  int32_t *label_buffers[2] = {labels, aux_labels};
  int active_idx = 0;

  // --- Frontier arrays ---
  uint8_t *active;
  uint8_t *next_active;
  if (posix_memalign((void **)&active, 64, n) != 0 ||
      posix_memalign((void **)&next_active, 64, n) != 0)
  {
    fprintf(stderr, "Memory allocation failed (frontiers)\n");
    free(aux_labels);
    exit(EXIT_FAILURE);
  }

  memset(active, 1, n); // all vertices active initially
  memset(next_active, 0, n);

  // --- Temporary array for active vertex indices ---
  int32_t *active_list = malloc(n * sizeof(int32_t));
  if (!active_list)
  {
    fprintf(stderr, "Memory allocation failed (active_list)\n");
    free(aux_labels);
    free(active);
    free(next_active);
    exit(EXIT_FAILURE);
  }

  while (1)
  {
    int changed = 0;
    const int read_idx = active_idx;
    const int write_idx = read_idx ^ 1;
    const int32_t *restrict current_labels = label_buffers[read_idx];
    int32_t *restrict next_labels = label_buffers[write_idx];

    // --- Build compact active list ---
    int active_count = 0;
    #pragma omp parallel
    {
      int32_t *local_buf = malloc((n / omp_get_num_threads() + 1) * sizeof(int32_t));
      int local_count = 0;

      #pragma omp for nowait schedule(static)
      for (int32_t u = 0; u < n; u++)
        if (active[u])
          local_buf[local_count++] = u;

      int offset;
      #pragma omp atomic capture
      offset = active_count += local_count;

      memcpy(active_list + offset - local_count, local_buf, local_count * sizeof(int32_t));
      free(local_buf);
    }

    if (active_count == 0)
      break;

    // --- Process active vertices ---
    #pragma omp parallel for schedule(dynamic, 64) reduction(|| : changed)
    for (int i = 0; i < active_count; i++)
    {
      int32_t u = active_list[i];
      int32_t new_label = current_labels[u];

      // Find smallest label among neighbors
      for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
      {
        int32_t v = col_idx[j];
        if (current_labels[v] < new_label)
          new_label = current_labels[v];
      }

      next_labels[u] = new_label;

      if (new_label < current_labels[u])
      {
        changed = 1;
        next_active[u] = 1;

        // Optionally activate neighbors (skip redundant writes)
        int64_t start = row_ptr[u];
        int64_t end = row_ptr[u + 1];
        for (int64_t j = start; j < end; j++)
        {
          int32_t v = col_idx[j];
          if (!next_active[v])
            next_active[v] = 1;
        }
      }
    }

    if (!changed)
      break;

    active_idx ^= 1;

    // --- Swap buffers ---
    uint8_t *tmp_frontier = active;
    active = next_active;
    next_active = tmp_frontier;

// --- Parallel clear next frontier ---
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++)
      next_active[i] = 0;
  }

  // --- Copy back final labels ---
  if (active_idx != 0)
  {
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++)
      labels[i] = label_buffers[active_idx][i];
  }

  // --- Cleanup ---
  free(aux_labels);
  free(active);
  free(next_active);
  free(active_list);
}

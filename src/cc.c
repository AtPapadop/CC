#define _POSIX_C_SOURCE 200112L

#include "cc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void compute_connected_components(const CSRGraph *restrict G, int32_t *restrict labels)
{
  const size_t n = (size_t)G->n;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;

  // Initialize labels
  for (size_t i = 0; i < n; i++)
    labels[i] = (int32_t)i;

  // Allocate aligned label buffer (double buffering)
  int32_t *aux_labels;
  if (posix_memalign((void **)&aux_labels, 64, n * sizeof(int32_t)) != 0)
  {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  int32_t *label_buffers[2] = {labels, aux_labels};
  int active_idx = 0;

  // Allocate frontier arrays
  uint8_t *active;
  uint8_t *next_active;
  if (posix_memalign((void **)&active, 64, n) != 0 || posix_memalign((void **)&next_active, 64, n) != 0)
  {
    fprintf(stderr, "Memory allocation failed (frontiers)\n");
    free(aux_labels);
    exit(EXIT_FAILURE);
  }

  memset(active, 1, n);      // initially all vertices active
  memset(next_active, 0, n); // next frontier empty

  while (1)
  {
    int changed = 0;
    const int read_idx = active_idx;
    const int write_idx = read_idx ^ 1;
    const int32_t *restrict current_labels = label_buffers[read_idx];
    int32_t *restrict next_labels = label_buffers[write_idx];

    for (size_t u = 0; u < n; u++)
    {
      int32_t new_label = current_labels[u];

      if (active[u])
      {
        // Find minimum label among neighbors
        for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
        {
          int32_t v = col_idx[j];
          if (current_labels[v] < new_label)
            new_label = current_labels[v];
        }

        // If label changed, activate this vertex and its neighbors
        if (new_label < current_labels[u])
        {
          changed = 1;
          next_active[u] = 1;
          for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
            next_active[col_idx[j]] = 1;
        }
      }

      next_labels[u] = new_label;
    }

    if (!changed)
      break;

    active_idx ^= 1;

    uint8_t *tmp_frontier = active;
    active = next_active;
    next_active = tmp_frontier;

    memset(next_active, 0, n); // clear next frontier for next round
  }

  if (active_idx != 0)
    memcpy(labels, label_buffers[active_idx], n * sizeof(int32_t));

  free(aux_labels);
  free(active);
  free(next_active);
}

void compute_connected_components_bfs(const CSRGraph *restrict G, int32_t *restrict labels)
{
  int32_t n = G->n;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;

  for (int32_t i = 0; i < n; i++)
    labels[i] = -1;

  int32_t *restrict queue;
  if (posix_memalign((void **)&queue, 64, n * sizeof(int32_t)) != 0)
  {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  int32_t current_label = 0;
  for (int32_t stc = 0; stc < n; stc++)
  {
    if (labels[stc] != -1)
      continue; // already visited

    labels[stc] = current_label;
    int32_t front = 0, back = 0;
    queue[back++] = stc;

    while (front < back)
    {
      int32_t u = queue[front++];
      for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
      {
        int32_t v = col_idx[j];
        if (labels[v] == -1)
        {
          labels[v] = current_label;
          queue[back++] = v;
        }
      }
    }
    current_label++;
  }

  free(queue);
}

int32_t count_unique_labels(const int32_t *restrict labels, int32_t n)
{
  int32_t *restrict unique_flags = (int32_t *)calloc(n, sizeof(int32_t));
  if (!unique_flags)
  {
    fprintf(stderr, "Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  int32_t unique_count = 0;
  for (int32_t i = 0; i < n; i++)
  {
    if (!unique_flags[labels[i]])
    {
      unique_flags[labels[i]] = 1;
      unique_count++;
    }
  }

  free(unique_flags);
  return unique_count;
}

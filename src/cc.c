#define _POSIX_C_SOURCE 200112L

#include "cc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void compute_connected_components(const CSRGraph *restrict G,
                                  int32_t *restrict labels)
{
  const size_t n = (size_t)G->n;
  const int64_t *restrict row_ptr = G->row_ptr;
  const int32_t *restrict col_idx = G->col_idx;

  // Initialize labels
  for (size_t i = 0; i < n; i++)
    labels[i] = (int32_t)i;

  while (1)
  {
    int changed = 0;

    for (size_t u = 0; u < n; u++)
    {
      int32_t old_label = labels[u];
      int32_t new_label = old_label;

      for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
      {
        int32_t v = col_idx[j];
        int32_t neighbor_label = labels[v];
        if (neighbor_label < new_label)
          new_label = neighbor_label;
      }

      if (new_label < old_label)
      {
        labels[u] = new_label;
        changed = 1;

        for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
        {
          int32_t v = col_idx[j];
          if (labels[v] > new_label)
            labels[v] = new_label;
        }
      }
    }

    if (!changed)
      break;
  }
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

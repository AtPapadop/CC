#include "cc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void compute_connected_components(const CSRGraph *G, int32_t *labels)
{
    int32_t n = G->n;
    int64_t *row_ptr = G->row_ptr;
    int32_t *col_idx = G->col_idx;

    // Initialize labels
    for (int32_t i = 0; i < n; i++)
        labels[i] = i;

    int32_t *new_labels = (int32_t *)malloc(n * sizeof(int32_t));
    if (!new_labels)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    int changed = 1;
    while (changed)
    {
        changed = 0;

        for (int32_t u = 0; u < n; u++)
        {
            new_labels[u] = labels[u];
            for (int64_t j = row_ptr[u]; j < row_ptr[u + 1]; j++)
            {
                int32_t v = col_idx[j];
                if (labels[v] < new_labels[u])
                {
                    new_labels[u] = labels[v];
                }
            }
            if (new_labels[u] < labels[u])
            {
                changed = 1;
            }
        }
        int32_t *temp = labels;
        labels = new_labels;
        new_labels = temp;
    }

    free(new_labels);
}

void compute_connected_components_bfs(const CSRGraph *G, int32_t *labels)
{
    int32_t n = G->n;
    int64_t *row_ptr = G->row_ptr;
    int32_t *col_idx = G->col_idx;

    for (int32_t i = 0; i < n; i++)
        labels[i] = -1;

    int32_t *queue = (int32_t *)malloc(n * sizeof(int32_t));
    if (!queue)
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
}

int32_t count_unique_labels(const int32_t *labels, int32_t n)
{
    int32_t *unique_flags = (int32_t *)calloc(n, sizeof(int32_t));
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
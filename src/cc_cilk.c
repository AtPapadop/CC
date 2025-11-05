#include "cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h>

void compute_connected_components_cilk(const CSRGraph *G, int32_t *labels)
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

        cilk_for (int32_t u = 0; u < n; u++)
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

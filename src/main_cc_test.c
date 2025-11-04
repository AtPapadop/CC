/* CC Test
 *
 * This program loads a graph from a Matrix Market file, computes its connected components,
 * counts the number of unique components, and writes the component labels to an output file.
 *
 * Usage: ./cc_test <matrix-market-file> <algorithm> <num-threads>
 *   <algorithm>: 'lp' for label propagation, 'bfs' for BFS
 *   <num-threads>: number of threads to use for parallel execution using OpenMP, in case of label propagation only
 *                  if the number of threads is 1, the sequential version is used
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <omp.h>
#include "cc.h"
#include "graph.h"

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <matrix-market-file> <algorithm> <num-threads>\n", argv[0]);
        fprintf(stderr, "Algorithm options: 'lp' for label propagation, 'bfs' for BFS\n");
        return EXIT_FAILURE;
    }

    const char *path = argv[1];
    const char *algorithm = argv[2];
    const char *threads_arg = argv[3];
    CSRGraph G;
    if (load_csr_from_mtx(path, 1, 1, &G) != 0)
    {
        fprintf(stderr, "Failed to load graph from %s\n", path);
        return EXIT_FAILURE;
    }

    int32_t *labels = (int32_t *)malloc(G.n * sizeof(int32_t));
    if (!labels)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free_csr(&G);
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    long requested_threads = strtol(threads_arg, &endptr, 10);
    if (*threads_arg == '\0' || *endptr != '\0' || requested_threads <= 0 || requested_threads > INT_MAX)
    {
        fprintf(stderr, "Invalid thread count: %s\n", threads_arg);
        free(labels);
        free_csr(&G);
        return EXIT_FAILURE;
    }
    int num_threads = (int)requested_threads;
    omp_set_num_threads(num_threads);

    if (strcmp(algorithm, "lp") == 0)
    {
        if (num_threads > 1)
        {
            compute_connected_components_omp(&G, labels);
        }
        else
        {
            compute_connected_components(&G, labels);
        }
    }
    else if (strcmp(algorithm, "bfs") == 0)
    {
        compute_connected_components_bfs(&G, labels);
    }
    else
    {
        fprintf(stderr, "Unknown algorithm: %s\n", algorithm);
        free(labels);
        free_csr(&G);
        return EXIT_FAILURE;
    }

    int32_t num_components = count_unique_labels(labels, G.n);

    printf("Number of connected components: %d\n", num_components);

    FILE *fout = fopen("c_labels.txt", "w");
    if (!fout)
    {
        fprintf(stderr, "Failed to open output file\n");
        free(labels);
        free_csr(&G);
        return EXIT_FAILURE;
    }

    for (int32_t i = 0; i < G.n; i++)
    {
        fprintf(fout, "%d\n", labels[i]);
    }

    fclose(fout);
    free(labels);
    free_csr(&G);
    return EXIT_SUCCESS;
}
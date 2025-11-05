/* CC Test
 *
 * This program loads a graph from a Matrix Market file, computes its connected components,
 * counts the number of unique components, and writes the component labels to an output file.
 *
 * Usage: ./cc_test <matrix-market-file> --algorithm lp|bfs --threads N
 *  <algorithm>: 'lp' for label propagation, 'bfs' for BFS (default: lp)
 *  <threads>: number of threads to use for parallel execution using OpenMP, in case of label propagation only (default: 1)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <omp.h>
#include <getopt.h>
#include "cc.h"
#include "graph.h"

int main(int argc, char **argv)
{
    /* Defaults */
    const char *algorithm = "lp";
    int num_threads = 1;
    const char *path = NULL;

    const struct option long_opts[] = {
        {"algorithm", required_argument, NULL, 'a'},
        {"threads", required_argument, NULL, 't'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "a:t:", long_opts, &opt_index)) != -1)
    {
        switch (opt)
        {
        case 'a':
            algorithm = optarg;
            break;
        case 't':
        {
            char *endptr = NULL;
            long parsed = strtol(optarg, &endptr, 10);
            if (optarg[0] == '\0' || *endptr != '\0' || parsed <= 0 || parsed > INT_MAX)
            {
                fprintf(stderr, "Invalid thread count: %s\n", optarg);
                return EXIT_FAILURE;
            }
            num_threads = (int)parsed;
        }
        break;
        default:
            fprintf(stderr, "Usage: %s [--algorithm lp|bfs] [--threads N] <matrix-market-file>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Remaining non-option argument should be the path */
    if (optind >= argc)
    {
        fprintf(stderr, "Missing matrix-market file path.\n");
        fprintf(stderr, "Usage: %s [--algorithm lp|bfs] [--threads N] <matrix-market-file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[optind];

    printf("Loading graph: %s\n", path);

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
    omp_set_num_threads(num_threads);

    printf("Computing connected components...\n");

    /* Time the connected components computation */
    double cc_start = omp_get_wtime();

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

    double cc_end = omp_get_wtime();
    printf("Connected components time: %.6f seconds\n", cc_end - cc_start);

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
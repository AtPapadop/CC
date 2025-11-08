/* CC Test (Pthreads)
 *
 * Loads a graph from a Matrix Market (.mtx/.txt) or MATLAB (.mat) file,
 * computes connected components using the label propagation algorithm
 * parallelized with Pthreads (dynamic scheduling), and writes the labels to a file.
 *
 * Usage:
 *   ./cc_test_pthreads <matrix-file-path> [--threads N]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>
#include <omp.h>  // only for timing
#include "cc.h"
#include "graph.h"

int main(int argc, char **argv)
{
    int num_threads = 1;
    int runs = 1;
    const char *path = NULL;

    const struct option long_opts[] = {
        {"threads", required_argument, NULL, 't'},
        {"runs", required_argument, NULL, 'r'},
        {NULL, 0, NULL, 0}
    };

    int opt, opt_index = 0;
    while ((opt = getopt_long(argc, argv, "t:r:", long_opts, &opt_index)) != -1)
    {
        switch (opt)
        {
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
            break;
        }
        case 'r':
        {
            char *endptr = NULL;
            long parsed = strtol(optarg, &endptr, 10);
            if (optarg[0] == '\0' || *endptr != '\0' || parsed <= 0 || parsed > INT_MAX)
            {
                fprintf(stderr, "Invalid run count: %s\n", optarg);
                return EXIT_FAILURE;
            }
            runs = (int)parsed;
            break;
        }
        default:
            fprintf(stderr, "Usage: %s [--threads N] [--runs N] <matrix-file-path>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Missing matrix file path.\n");
        fprintf(stderr, "Usage: %s [--threads N] [--runs N] <matrix-file-path>\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[optind];

    printf("Loading graph: %s\n", path);

    CSRGraph G;
    if (load_csr_from_file(path, 1, 1, &G) != 0)
    {
        fprintf(stderr, "Failed to load graph from %s\n", path);
        return EXIT_FAILURE;
    }

    int32_t *labels = malloc(G.n * sizeof(int32_t));
    if (!labels)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free_csr(&G);
        return EXIT_FAILURE;
    }

    printf("Computing connected components with %d thread%s (%d run%s)...\n",
        num_threads,
        num_threads == 1 ? "" : "s",
        runs,
        runs == 1 ? "" : "s");

    double total_time = 0.0;
    for (int run = 0; run < runs; run++)
    {
     double start = omp_get_wtime();
     compute_connected_components_pthreads(&G, labels, num_threads);
     double elapsed = omp_get_wtime() - start;
     total_time += elapsed;
     printf("Run %d time: %.6f seconds\n", run + 1, elapsed);
    }

    double average = total_time / runs;
    printf("Average time over %d run%s: %.6f seconds.\n",
        runs,
        runs == 1 ? "" : "s",
        average);

    int32_t num_components = count_unique_labels(labels, G.n);
    printf("Number of connected components: %d\n", num_components);

    FILE *fout = fopen("pthread_labels.txt", "w");
    if (!fout)
    {
        fprintf(stderr, "Failed to open output file.\n");
        free(labels);
        free_csr(&G);
        return EXIT_FAILURE;
    }

    for (int32_t i = 0; i < G.n; i++)
        fprintf(fout, "%d\n", labels[i]);

    fclose(fout);
    printf("Labels written to pthread_labels.txt\n");

    free(labels);
    free_csr(&G);
    return EXIT_SUCCESS;
}

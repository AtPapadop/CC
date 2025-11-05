/* CC Test (OpenCilk)
 *
 * Loads a graph from a Matrix Market file, computes connected components using
 * label propagation (LP) with OpenCilk parallelism, and writes labels to a file.
 *
 * Usage:
 *   ./cc_test_cilk <matrix-market-file> [--threads N]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>
#include <time.h>
#include <stdint.h>
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include "cc.h"
#include "graph.h"

// Function to get wall-clock time in seconds
static inline double wall_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}


int main(int argc, char **argv)
{
    const char *path = NULL;
    int num_threads = 1;

    const struct option long_opts[] = {
        {"threads", required_argument, NULL, 't'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "t:", long_opts, &opt_index)) != -1)
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
        }
        break;
        default:
            fprintf(stderr, "Usage: %s [--threads N] <matrix-market-file>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Missing matrix-market file path.\n");
        fprintf(stderr, "Usage: %s [--threads N] <matrix-market-file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[optind];

    // Set Cilk worker count
    char workers_env[64];
    snprintf(workers_env, sizeof(workers_env), "CILK_NWORKERS=%d", num_threads);
    putenv(workers_env);

    printf("Loading graph: %s\n", path);

    CSRGraph G;
    if (load_csr_from_mtx(path, 1, 1, &G) != 0)
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

    printf("Computing connected components...\n");

    double start = wall_time();
    compute_connected_components_cilk(&G, labels);
    double end = wall_time();

    int32_t num_components = count_unique_labels(labels, G.n);
    printf("Number of connected components: %d\n", num_components);
    printf("Execution time: %.3f seconds\n", end - start);

    FILE *fout = fopen("cilk_labels.txt", "w");
    if (!fout)
    {
        fprintf(stderr, "Failed to open output file\n");
        free(labels);
        free_csr(&G);
        return EXIT_FAILURE;
    }
    for (int32_t i = 0; i < G.n; i++)
        fprintf(fout, "%d\n", labels[i]);
    fclose(fout);

    printf("Labels written to cilk_labels.txt\n");

    free(labels);
    free_csr(&G);
    return EXIT_SUCCESS;
}

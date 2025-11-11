/* CC Test (OpenCilk)
 *
 * Loads a graph from a Matrix Market or MATLAB file, computes connected components using
 * label propagation (LP) with OpenCilk parallelism, and writes labels to a file.
 *
 * Usage:
 *   CILK_NWORKERS=N ./cc_test_cilk <matrix-file>
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <stdint.h>
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <getopt.h>
#include "cc.h"
#include "graph.h"
#include "results_writer.h"

// Wall-clock time in seconds
static inline double wall_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int runs = 1;
    const char *path = NULL;
    const char *output_dir = "results";

    const struct option long_opts[] = {
        {"runs", required_argument, NULL, 'r'},
        {"output", required_argument, NULL, 'o'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "r:o:", long_opts, &opt_index)) != -1)
    {
        switch (opt)
        {
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
        case 'o':
            if (optarg[0] == '\0')
            {
                fprintf(stderr, "Output directory must not be empty.\n");
                return EXIT_FAILURE;
            }
            output_dir = optarg;
            break;
        default:
            fprintf(stderr, "Usage: %s [--runs N] [--output DIR] <matrix-file>\n", argv[0]);
            fprintf(stderr, "Example: CILK_NWORKERS=8 %s data/graph.mtx\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Usage: %s [--runs N] [--output DIR] <matrix-file>\n", argv[0]);
        fprintf(stderr, "Example: CILK_NWORKERS=8 %s data/graph.mtx\n", argv[0]);
        return EXIT_FAILURE;
    }

    path = argv[optind];

    if (results_writer_ensure_directory(output_dir) != 0)
    {
        fprintf(stderr, "Failed to create output directory '%s': %s\n", output_dir, strerror(errno));
        return EXIT_FAILURE;
    }

    const char *method_base = "cilk";
    char method_name[32];
    snprintf(method_name, sizeof(method_name), "%s", method_base);

    char labels_filename[64];
    snprintf(labels_filename, sizeof(labels_filename), "%s_labels.txt", method_name);

    char labels_path[PATH_MAX];
    if (results_writer_join_path(labels_path, sizeof(labels_path), output_dir, labels_filename) != 0)
    {
        fprintf(stderr, "Output path too long for labels file: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // Report worker configuration
    int workers = __cilkrts_get_nworkers();
    printf("OpenCilk workers: %d\n", workers);

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

    double *run_times = (double *)malloc((size_t)runs * sizeof(double));
    if (!run_times)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free(labels);
        free_csr(&G);
        return EXIT_FAILURE;
    }

    printf("Computing connected components (%d run%s)...\n", runs, runs == 1 ? "" : "s");

    double total_time = 0.0;
    for (int run = 0; run < runs; run++)
    {
        double start = wall_time();
        compute_connected_components_cilk(&G, labels);
        double end = wall_time();
        double elapsed = end - start;
        total_time += elapsed;
        printf("Run %d time: %.6f seconds\n", run + 1, elapsed);
        run_times[run] = elapsed;
    }

    double average = total_time / runs;
    printf("Average time over %d run%s: %.6f seconds\n", runs, runs == 1 ? "" : "s", average);

    char column_name[64];
    snprintf(column_name, sizeof(column_name), "%d Threads", workers);

    int results_path_ready = 0;
    char results_path[PATH_MAX];
    results_path[0] = '\0';

    char results_prefix[32];
    snprintf(results_prefix, sizeof(results_prefix), "results_%s", method_base);

    if (results_writer_build_results_path(results_path, sizeof(results_path), output_dir, results_prefix, path) != 0)
    {
        fprintf(stderr, "Warning: Failed to build results path: %s\n", strerror(errno));
    }
    else
    {
        results_path_ready = 1;
        results_writer_status csv_status = append_times_column(results_path, column_name, run_times, (size_t)runs);
        if (csv_status != RESULTS_WRITER_OK)
        {
            fprintf(stderr, "Warning: Failed to update %s (error %d)\n", results_path, (int)csv_status);
        }
    }

    int32_t num_components = count_unique_labels(labels, G.n);
    printf("Number of connected components: %d\n", num_components);

    FILE *fout = fopen(labels_path, "w");
    if (!fout)
    {
        fprintf(stderr, "Failed to open output file %s\n", labels_path);
        free(run_times);
        free(labels);
        free_csr(&G);
        return EXIT_FAILURE;
    }
    for (int32_t i = 0; i < G.n; i++)
        fprintf(fout, "%d\n", labels[i]);
    fclose(fout);

    printf("Labels written to %s\n", labels_path);
    if (results_path_ready)
        printf("Time results written to %s\n", results_path);

    free(run_times);
    free(labels);
    free_csr(&G);
    return EXIT_SUCCESS;
}

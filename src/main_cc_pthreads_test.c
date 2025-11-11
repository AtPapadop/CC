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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>
#include <omp.h>  // only for timing
#include "cc.h"
#include "graph.h"
#include "results_writer.h"

int main(int argc, char **argv)
{
    int num_threads = 1;
    int runs = 1;
    const char *path = NULL;
    const char *output_dir = "results";

    const struct option long_opts[] = {
        {"threads", required_argument, NULL, 't'},
        {"runs", required_argument, NULL, 'r'},
        {"output", required_argument, NULL, 'o'},
        {NULL, 0, NULL, 0}
    };

    int opt, opt_index = 0;
    while ((opt = getopt_long(argc, argv, "t:r:o:", long_opts, &opt_index)) != -1)
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
        case 'o':
            if (optarg[0] == '\0')
            {
                fprintf(stderr, "Output directory must not be empty.\n");
                return EXIT_FAILURE;
            }
            output_dir = optarg;
            break;
        default:
            fprintf(stderr, "Usage: %s [--threads N] [--runs N] [--output DIR] <matrix-file-path>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Missing matrix file path.\n");
        fprintf(stderr, "Usage: %s [--threads N] [--runs N] [--output DIR] <matrix-file-path>\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[optind];

    if (results_writer_ensure_directory(output_dir) != 0)
    {
        fprintf(stderr, "Failed to create output directory '%s': %s\n", output_dir, strerror(errno));
        return EXIT_FAILURE;
    }

    char labels_path[PATH_MAX];
    if (results_writer_join_path(labels_path, sizeof(labels_path), output_dir, "pthread_labels.txt") != 0)
    {
        fprintf(stderr, "Output path too long for labels file: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

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
        run_times[run] = elapsed;
    }

    double average = total_time / runs;
    printf("Average time over %d run%s: %.6f seconds.\n",
        runs,
        runs == 1 ? "" : "s",
        average);

    char column_name[64];
    snprintf(column_name, sizeof(column_name), "%d Threads", num_threads);
    char results_path[PATH_MAX];
    if (results_writer_join_path(results_path, sizeof(results_path), output_dir, "results_pthred.csv") != 0)
    {
        fprintf(stderr, "Warning: Output path too long for results file: %s\n", strerror(errno));
    }
    else
    {
        results_writer_status csv_status = append_times_column(results_path, column_name, run_times, (size_t)runs);
        if (csv_status != RESULTS_WRITER_OK)
            fprintf(stderr, "Warning: Failed to update %s (error %d)\n", results_path, (int)csv_status);
    }

    int32_t num_components = count_unique_labels(labels, G.n);
    printf("Number of connected components: %d\n", num_components);

    FILE *fout = fopen(labels_path, "w");
    if (!fout)
    {
        fprintf(stderr, "Failed to open output file %s.\n", labels_path);
        free(labels);
        free_csr(&G);
        free(run_times);
        return EXIT_FAILURE;
    }

    for (int32_t i = 0; i < G.n; i++)
        fprintf(fout, "%d\n", labels[i]);

    fclose(fout);
    printf("Labels written to %s\n", labels_path);
    printf("Time results written to %s\n", results_path);

    free(run_times);
    free(labels);
    free_csr(&G);
    return EXIT_SUCCESS;
}

/* CC Test
 *
 * This program loads a graph from a Matrix Market file, computes its connected components,
 * counts the number of unique components, and writes the component labels to an output file.
 *
 * Usage: ./cc_test <matrix-file-path> --algorithm lp|bfs --threads N
 *  <algorithm>: 'lp' for label propagation, 'bfs' for BFS (default: lp)
 *  <threads>: number of threads to use for parallel execution using OpenMP, in case of label propagation only (default: 1)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <omp.h>
#include <getopt.h>
#include "cc.h"
#include "graph.h"
#include "results_writer.h"

int main(int argc, char **argv)
{
    /* Defaults */
    const char *algorithm = "lp";
    int num_threads = 1;
    int runs = 1;
    const char *path = NULL;
    const char *output_dir = "results";

    const struct option long_opts[] = {
        {"algorithm", required_argument, NULL, 'a'},
        {"threads", required_argument, NULL, 't'},
        {"runs", required_argument, NULL, 'r'},
        {"output", required_argument, NULL, 'o'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "a:t:r:o:", long_opts, &opt_index)) != -1)
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
        }
        break;
        case 'o':
            if (optarg[0] == '\0')
            {
                fprintf(stderr, "Output directory must not be empty.\n");
                return EXIT_FAILURE;
            }
            output_dir = optarg;
            break;
        default:
            fprintf(stderr, "Usage: %s [--algorithm lp|bfs] [--threads N] [--runs N] [--output DIR] <matrix-market-file>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Remaining non-option argument should be the path */
    if (optind >= argc)
    {
        fprintf(stderr, "Missing matrix file path.\n");
        fprintf(stderr, "Usage: %s [--algorithm lp|bfs] [--threads N] [--runs N] [--output DIR] <matrix-file-path>\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[optind];

    if (results_writer_ensure_directory(output_dir) != 0)
    {
        fprintf(stderr, "Failed to create output directory '%s': %s\n", output_dir, strerror(errno));
        return EXIT_FAILURE;
    }

    char method_base[16] = "";
    if (strcmp(algorithm, "lp") == 0)
    {
        if (num_threads > 1)
            strncpy(method_base, "omp", sizeof(method_base) - 1);
        else
            strncpy(method_base, "c", sizeof(method_base) - 1);
    }
    else if (strcmp(algorithm, "bfs") == 0)
    {
        strncpy(method_base, "bfs", sizeof(method_base) - 1);
    }

    char method_name[32] = "";
    if (method_base[0] != '\0')
    {
        snprintf(method_name, sizeof(method_name), "%s", method_base);
    }

    char labels_filename[64] = "c_labels.txt";
    if (method_name[0] != '\0')
        snprintf(labels_filename, sizeof(labels_filename), "%s_labels.txt", method_name);

    char labels_path[PATH_MAX];
    if (results_writer_join_path(labels_path, sizeof(labels_path), output_dir, labels_filename) != 0)
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

    int32_t *labels = (int32_t *)malloc(G.n * sizeof(int32_t));
    if (!labels)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free_csr(&G);
        return EXIT_FAILURE;
    }
    omp_set_num_threads(num_threads);

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
            free(run_times);
            free(labels);
            free_csr(&G);
            return EXIT_FAILURE;
        }

        double elapsed = omp_get_wtime() - cc_start;
        total_time += elapsed;
        printf("Run %d time: %.6f seconds\n", run + 1, elapsed);
        run_times[run] = elapsed;
    }

    double average_time = total_time / runs;
    printf("Average time over %d run%s: %.6f seconds\n", runs, runs == 1 ? "" : "s", average_time);

    char column_name[64] = "";
    const char *results_prefix = NULL;

    char results_path[PATH_MAX];
    results_path[0] = '\0';
    int results_path_ready = 0;

    if (strcmp(algorithm, "lp") == 0)
    {
        if (num_threads == 1)
            snprintf(column_name, sizeof(column_name), "1 Thread");
        else
            snprintf(column_name, sizeof(column_name), "%d Threads", num_threads);

        results_prefix = "results_omp";
    }
    else if (strcmp(algorithm, "bfs") == 0)
    {
        snprintf(column_name, sizeof(column_name), "BFS");
        results_prefix = "results_bfs";
    }

    if (results_prefix && column_name[0] != '\0')
    {
        if (results_writer_build_results_path(results_path, sizeof(results_path), output_dir, results_prefix, path) != 0)
        {
            fprintf(stderr, "Warning: Failed to build results path: %s\n", strerror(errno));
        }
        else
        {
            results_path_ready = 1;
            results_writer_status status = append_times_column(results_path, column_name, run_times, (size_t)runs);
            if (status != RESULTS_WRITER_OK)
                fprintf(stderr, "Warning: Failed to update %s (error %d)\n", results_path, (int)status);
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
    {
        fprintf(fout, "%d\n", labels[i]);
    }

    fclose(fout);

    printf("Labels written to %s\n", labels_path);
    if (results_path_ready)
        printf("Time results written to %s\n", results_path);

    free(run_times);
    free(labels);
    free_csr(&G);
    return EXIT_SUCCESS;
}

/* CC Test (OpenMP label propagation)
 *
 * Loads a graph and benchmarks the OpenMP LP kernel across a user-provided set of
 * thread counts (range/list syntax), averaging multiple runs per configuration and
 * appending the timing columns to the standard results CSV files.
 * 
 * Usage:
 *  ./cc_omp [OPTIONS] <matrix-file-path>
 * See --help for accepted specifications.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc.h"
#include "graph.h"
#include "opt_parser.h"
#include "results_writer.h"

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS] <matrix-file-path>\n\n"
            "Options:\n"
            "  -t, --threads SPEC        Thread counts (comma list or start:end[:step], default 1)\n"
            "  -c, --chunk-size N        Chunk size for OpenMP scheduling (default 2048)\n"
            "  -r, --runs N              Runs per thread count (default 1)\n"
            "  -o, --output DIR          Output directory (default 'results')\n"
            "  -h, --help                Show this message\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *thread_spec = "1";
    int chunk_size = 2048;
    int runs = 1;
    const char *output_dir = "results";
    const char *matrix_path = NULL;

    const struct option long_opts[] = {
        {"threads", required_argument, NULL, 't'},
        {"chunk-size", required_argument, NULL, 'c'},
        {"runs", required_argument, NULL, 'r'},
        {"output", required_argument, NULL, 'o'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}};

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "t:c:r:o:h", long_opts, &opt_index)) != -1)
    {
        switch (opt)
        {
        case 't':
            if (!optarg || *optarg == '\0')
            {
                fprintf(stderr, "Thread specification must not be empty.\n");
                return EXIT_FAILURE;
            }
            thread_spec = optarg;
            break;
        case 'c':
            if (opt_parse_positive_int(optarg, &chunk_size) != 0)
            {
                fprintf(stderr, "Invalid chunk size: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'r':
            if (opt_parse_positive_int(optarg, &runs) != 0)
            {
                fprintf(stderr, "Invalid run count: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'o':
            if (!optarg || *optarg == '\0')
            {
                fprintf(stderr, "Output directory must not be empty.\n");
                return EXIT_FAILURE;
            }
            output_dir = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Missing matrix file path.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    matrix_path = argv[optind];

    if (results_writer_ensure_directory(output_dir) != 0)
    {
        fprintf(stderr, "Failed to create output directory '%s': %s\n", output_dir, strerror(errno));
        return EXIT_FAILURE;
    }

    OptIntList thread_counts;
    opt_int_list_init(&thread_counts);
    if (opt_parse_range_list(thread_spec, &thread_counts, "thread counts") != 0)
    {
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }

    printf("Sweeping %zu thread option%s (%d run%s each).\n",
           thread_counts.size,
           thread_counts.size == 1 ? "" : "s",
           runs,
           runs == 1 ? "" : "s");

    CSRGraph G;
    if (load_csr_from_file(matrix_path, 1, 1, &G) != 0)
    {
        fprintf(stderr, "Failed to load graph from %s\n", matrix_path);
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }

    int32_t *labels = (int32_t *)malloc((size_t)G.n * sizeof(int32_t));
    if (!labels)
    {
        fprintf(stderr, "Label allocation failed (n=%d).\n", G.n);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }

    double *run_times = (double *)malloc((size_t)runs * sizeof(double));
    if (!run_times)
    {
        fprintf(stderr, "Memory allocation failed.\n");
        free(labels);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }

    char labels_path[PATH_MAX];
    if (results_writer_join_path(labels_path, sizeof(labels_path), output_dir, "omp_labels.txt") != 0)
    {
        fprintf(stderr, "Output path too long for labels file: %s\n", strerror(errno));
        free(run_times);
        free(labels);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }

    char results_path[PATH_MAX];
    if (results_writer_build_results_path(results_path, sizeof(results_path), output_dir, "results_omp", matrix_path) != 0)
    {
        fprintf(stderr, "Failed to build results path: %s\n", strerror(errno));
        free(run_times);
        free(labels);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }

    for (size_t idx = 0; idx < thread_counts.size; idx++)
    {
        int threads = thread_counts.values[idx];
        printf("Running LP with %d thread%s (%d run%s)...\n",
               threads,
               threads == 1 ? "" : "s",
               runs,
               runs == 1 ? "" : "s");

        omp_set_num_threads(threads);
        double total_time = 0.0;
        for (int run = 0; run < runs; run++)
        {
            double start = omp_get_wtime();
            compute_connected_components_omp(&G, labels, chunk_size);
            double elapsed = omp_get_wtime() - start;
            total_time += elapsed;
            run_times[run] = elapsed;
            printf("  Run %d: %.6f seconds\n", run + 1, elapsed);
        }

        double average = total_time / runs;
        printf("Average for %d thread%s: %.6f seconds\n",
               threads,
               threads == 1 ? "" : "s",
               average);

        char column_name[64];
        snprintf(column_name, sizeof(column_name), threads == 1 ? "1 Thread" : "%d Threads", threads);
        results_writer_status status = append_times_column(results_path, column_name, run_times, (size_t)runs);
        if (status != RESULTS_WRITER_OK)
            fprintf(stderr, "Warning: Failed to update %s (error %d)\n", results_path, (int)status);
    }

    int32_t components = count_unique_labels(labels, G.n);
    printf("Number of connected components (last run): %d\n", components);

    FILE *fout = fopen(labels_path, "w");
    if (!fout)
    {
        fprintf(stderr, "Failed to open output file %s\n", labels_path);
        free(run_times);
        free(labels);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }
    for (int32_t i = 0; i < G.n; i++)
        fprintf(fout, "%d\n", labels[i]);
    fclose(fout);
    printf("Labels written to %s\n", labels_path);
    printf("Timing results written to %s\n", results_path);

    free(run_times);
    free(labels);
    free_csr(&G);
    opt_int_list_free(&thread_counts);
    return EXIT_SUCCESS;
}

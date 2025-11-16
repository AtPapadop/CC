/*
 * Connected Components using POSIX Threads - Parameter Sweep Tool
 *
 * Loads a graph, sweeps across lists/ranges of thread counts and chunk sizes,
 * executes multiple runs per configuration, and emits a compact CSV of
 * (threads, chunk_size, average_seconds) values suitable for 3D surface plots.
 *
 * Usage:
 *   ./cc_pthreads_sweep [OPTIONS] <matrix-file-path>
 * See --help for accepted specifications.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc.h"
#include "graph.h"
#include "results_writer.h"
#include "opt_parser.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS] <matrix-file-path>\n"
            "\n"
            "Options:\n"
            "  -t, --threads SPEC        Thread counts to sweep (comma list or start:end[:step])\n"
            "  -c, --chunk-size SPEC     Chunk sizes to sweep (comma list or start:end[:step])\n"
            "  -r, --runs N              Runs per configuration (default 100)\n"
            "  -o, --output DIR          Directory for result CSV (default 'results')\n"
            "  -h, --help                Show this message\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *thread_spec = "1";
    const char *chunk_spec = "4096";
    const char *output_dir = "results";
    int runs = 100;

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
            if (!optarg || *optarg == '\0')
            {
                fprintf(stderr, "Chunk-size specification must not be empty.\n");
                return EXIT_FAILURE;
            }
            chunk_spec = optarg;
            break;
        case 'r':
        {
            int parsed = 0;
            if (opt_parse_positive_int(optarg, &parsed) != 0)
            {
                fprintf(stderr, "Invalid run count: %s\n", optarg);
                return EXIT_FAILURE;
            }
            runs = parsed;
            break;
        }
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

    if (runs <= 0)
    {
        fprintf(stderr, "Number of runs must be positive.\n");
        return EXIT_FAILURE;
    }

    const char *matrix_path = argv[optind];

    if (results_writer_ensure_directory(output_dir) != 0)
    {
        fprintf(stderr, "Failed to prepare output directory '%s': %s\n", output_dir, strerror(errno));
        return EXIT_FAILURE;
    }

    OptIntList thread_counts;
    OptIntList chunk_sizes;
    opt_int_list_init(&thread_counts);
    opt_int_list_init(&chunk_sizes);

    if (opt_parse_range_list(thread_spec, &thread_counts, "thread counts") != 0)
    {
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }
    if (opt_parse_range_list(chunk_spec, &chunk_sizes, "chunk sizes") != 0)
    {
        opt_int_list_free(&thread_counts);
        opt_int_list_free(&chunk_sizes);
        return EXIT_FAILURE;
    }

    printf("Sweeping %zu thread option%s x %zu chunk-size option%s (%d run%s each).\n",
           thread_counts.size,
           thread_counts.size == 1 ? "" : "s",
           chunk_sizes.size,
           chunk_sizes.size == 1 ? "" : "s",
           runs,
           runs == 1 ? "" : "s");

    CSRGraph G;
    if (load_csr_from_file(matrix_path, 1, 1, &G) != 0)
    {
        fprintf(stderr, "Failed to load graph from %s\n", matrix_path);
        opt_int_list_free(&thread_counts);
        opt_int_list_free(&chunk_sizes);
        return EXIT_FAILURE;
    }

    int32_t *labels = (int32_t *)malloc((size_t)G.n * sizeof(int32_t));
    if (!labels)
    {
        fprintf(stderr, "Label allocation failed (n=%d).\n", G.n);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        opt_int_list_free(&chunk_sizes);
        return EXIT_FAILURE;
    }

    char results_path[PATH_MAX];
    if (results_writer_build_results_path(results_path, sizeof(results_path), output_dir,
                                          "results_pthread_surface", matrix_path) != 0)
    {
        fprintf(stderr, "Failed to build output path: %s\n", strerror(errno));
        free(labels);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        opt_int_list_free(&chunk_sizes);
        return EXIT_FAILURE;
    }

    bool append = false;
    FILE *existing = fopen(results_path, "r");
    if (existing)
    {
        append = true;
        fclose(existing);
    }
    else if (errno != ENOENT)
    {
        fprintf(stderr, "Failed to inspect %s: %s\n", results_path, strerror(errno));
        free(labels);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        opt_int_list_free(&chunk_sizes);
        return EXIT_FAILURE;
    }

    FILE *csv = fopen(results_path, append ? "a" : "w");
    if (!csv)
    {
        fprintf(stderr, "Failed to open %s for writing: %s\n", results_path, strerror(errno));
        free(labels);
        free_csr(&G);
        opt_int_list_free(&thread_counts);
        opt_int_list_free(&chunk_sizes);
        return EXIT_FAILURE;
    }

    if (!append)
    {
        if (fprintf(csv, "threads,chunk_size,average_seconds\n") < 0)
        {
            fprintf(stderr, "Failed to write CSV header to %s.\n", results_path);
            fclose(csv);
            free(labels);
            free_csr(&G);
            opt_int_list_free(&thread_counts);
            opt_int_list_free(&chunk_sizes);
            return EXIT_FAILURE;
        }
    }

    int32_t reference_components = -1;
    size_t total_configs = thread_counts.size * chunk_sizes.size;
    size_t completed = 0;

    for (size_t ti = 0; ti < thread_counts.size; ti++)
    {
        int threads = thread_counts.values[ti];
        for (size_t ci = 0; ci < chunk_sizes.size; ci++)
        {
            int chunk = chunk_sizes.values[ci];
            double total_time = 0.0;
            for (int run = 0; run < runs; run++)
            {
                double start = omp_get_wtime();
                compute_connected_components_pthreads(&G, labels, threads, chunk);
                double elapsed = omp_get_wtime() - start;
                total_time += elapsed;
            }

            double average = total_time / (double)runs;
            if (fprintf(csv, "%d,%d,%.6f\n", threads, chunk, average) < 0)
            {
                fprintf(stderr, "Failed to append row to %s.\n", results_path);
                fclose(csv);
                free(labels);
                free_csr(&G);
                opt_int_list_free(&thread_counts);
                opt_int_list_free(&chunk_sizes);
                return EXIT_FAILURE;
            }

            if (reference_components < 0)
                reference_components = count_unique_labels(labels, G.n);

            completed++;
            printf("[%zu/%zu] Threads=%d, Chunk=%d => average %.6f seconds over %d run%s.\n",
                   completed,
                   total_configs,
                   threads,
                   chunk,
                   average,
                   runs,
                   runs == 1 ? "" : "s");
        }
    }

    fclose(csv);

    if (reference_components >= 0)
        printf("Detected %d connected components.\n", reference_components);

    printf("3D sweep results saved to %s\n", results_path);

    free(labels);
    free_csr(&G);
    opt_int_list_free(&thread_counts);
    opt_int_list_free(&chunk_sizes);
    return EXIT_SUCCESS;
}

/* CC Test (Pthreads)
 *
 * Loads a Matrix Market (.mtx/.txt) or MATLAB (.mat) graph, runs the pthread-based
 * label propagation implementation for a single thread count, and writes both the
 * component labels and timing results. Thread counts accept either a single value
 * (default 1) or the range/list syntax shared with the sweep tool.
 *
 * Usage:
 *   ./cc_pthreads [OPTIONS] <matrix-file-path>
 * See --help for the full list of options.
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
#include "opt_parser.h"
#include "results_writer.h"

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS] <matrix-file-path>\n\n"
            "Options:\n"
            "  -t, --threads SPEC     Thread count (default 1; comma/range syntax supported)\n"
            "  -r, --runs N           Number of runs to average (default 1)\n"
            "  -o, --output DIR       Output directory (default 'results')\n"
            "  -c, --chunk-size N     Chunk size for dynamic scheduling (default 4096)\n"
            "  -h, --help             Show this message\n",
            prog);
}

int main(int argc, char **argv)
{
    int num_threads = 1;
    int runs = 1;
    int chunk_size = 4096;
    const char *path = NULL;
    const char *output_dir = "results";
    const char *thread_spec = "1";

    const struct option long_opts[] = {
        {"threads", required_argument, NULL, 't'},
        {"runs", required_argument, NULL, 'r'},
        {"output", required_argument, NULL, 'o'},
        {"chunk-size", required_argument, NULL, 'c'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt, opt_index = 0;
    while ((opt = getopt_long(argc, argv, "t:r:o:c:h", long_opts, &opt_index)) != -1)
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
        case 'r':
        {
            int parsed = 0;
            if (opt_parse_positive_int(optarg, &parsed) != 0)
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
        case 'c':
        {
            if (opt_parse_positive_int(optarg, &chunk_size) != 0)
            {
                fprintf(stderr, "Invalid chunk size: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        }
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
    path = argv[optind];

    OptIntList thread_counts;
    opt_int_list_init(&thread_counts);
    if (opt_parse_range_list(thread_spec, &thread_counts, "thread count") != 0)
    {
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }
    if (thread_counts.size != 1)
    {
        fprintf(stderr, "Please specify exactly one thread count for this binary (use cc_pthreads_sweep for sweeps).\n");
        opt_int_list_free(&thread_counts);
        return EXIT_FAILURE;
    }
    num_threads = thread_counts.values[0];
    opt_int_list_free(&thread_counts);

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

    printf("Computing connected components with %d thread%s, chunk size %d (%d run%s)...\n",
        num_threads,
        num_threads == 1 ? "" : "s",
        chunk_size,
        runs,
        runs == 1 ? "" : "s");

    double total_time = 0.0;
    for (int run = 0; run < runs; run++)
    {
        double start = omp_get_wtime();
    compute_connected_components_pthreads(&G, labels, num_threads, chunk_size);
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

    int results_path_ready = 0;
    char results_path[PATH_MAX];
    results_path[0] = '\0';
    if (results_writer_build_results_path(results_path, sizeof(results_path), output_dir, "results_pthread", path) != 0)
    {
        fprintf(stderr, "Warning: Failed to build results path: %s\n", strerror(errno));
    }
    else
    {
        results_path_ready = 1;
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
    if (results_path_ready)
        printf("Time results written to %s\n", results_path);

    free(run_times);
    free(labels);
    free_csr(&G);
    return EXIT_SUCCESS;
}

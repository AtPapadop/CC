/* CC Test (sequential LP + BFS)
 *
 * Loads a graph, runs either sequential label propagation or BFS connected
 * components, emits label files, and appends runtimes to the CSV summaries.
 * Algorithm selection and run counts are exposed via --algorithm/--runs flags.
 */

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
            "  -a, --algorithm lp|bfs   Algorithm to execute (default lp)\n"
            "  -r, --runs N             Number of runs to average (default 1)\n"
            "  -o, --output DIR         Output directory (default 'results')\n"
            "  -h, --help               Show this message\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *algorithm = "lp";
    int runs = 1;
    const char *path = NULL;
    const char *output_dir = "results";

    const struct option long_opts[] = {
        {"algorithm", required_argument, NULL, 'a'},
        {"runs", required_argument, NULL, 'r'},
        {"output", required_argument, NULL, 'o'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}};

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "a:r:o:h", long_opts, &opt_index)) != -1)
    {
        switch (opt)
        {
        case 'a':
            algorithm = optarg;
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
    path = argv[optind];

    if (strcmp(algorithm, "lp") != 0 && strcmp(algorithm, "bfs") != 0)
    {
        fprintf(stderr, "Unsupported algorithm '%s'. Choose 'lp' or 'bfs'.\n", algorithm);
        return EXIT_FAILURE;
    }

    if (results_writer_ensure_directory(output_dir) != 0)
    {
        fprintf(stderr, "Failed to create output directory '%s': %s\n", output_dir, strerror(errno));
        return EXIT_FAILURE;
    }

    const char *method_base = (strcmp(algorithm, "bfs") == 0) ? "bfs" : "c";
    char labels_filename[64];
    snprintf(labels_filename, sizeof(labels_filename), "%s_labels.txt", method_base);

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
        double start = omp_get_wtime();
        if (strcmp(algorithm, "lp") == 0)
            compute_connected_components(&G, labels);
        else
            compute_connected_components_bfs(&G, labels);
        double elapsed = omp_get_wtime() - start;
        total_time += elapsed;
        printf("Run %d time: %.6f seconds\n", run + 1, elapsed);
        run_times[run] = elapsed;
    }

    double average_time = total_time / runs;
    printf("Average time over %d run%s: %.6f seconds\n", runs, runs == 1 ? "" : "s", average_time);

    char column_name[64];
    const char *results_prefix = NULL;
    if (strcmp(algorithm, "lp") == 0)
    {
        snprintf(column_name, sizeof(column_name), "1 Thread");
        results_prefix = "results_omp";
    }
    else
    {
        snprintf(column_name, sizeof(column_name), "BFS");
        results_prefix = "results_bfs";
    }

    char results_path[PATH_MAX] = "";
    int results_path_ready = 0;
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

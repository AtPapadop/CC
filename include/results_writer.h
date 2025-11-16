#ifndef RESULTS_WRITER_H
#define RESULTS_WRITER_H

#include <stddef.h>

typedef enum
{
    RESULTS_WRITER_OK = 0,
    RESULTS_WRITER_IO_ERROR = -1,
    RESULTS_WRITER_MEMORY_ERROR = -2,
    RESULTS_WRITER_INVALID_ARGS = -3
} results_writer_status;

// Append a column of timing results to a CSV file.
// Parameters:
//   filename    – existing or new CSV file to modify/append
//   column_name – header label written to the CSV
//   values      – array of `count` timing values (seconds)
//   count       – number of rows to write
// Returns RESULTS_WRITER_OK on success or an error status otherwise.
results_writer_status append_times_column(const char *filename, const char *column_name,
                                          const double *values, size_t count);

// Ensure that the directory at 'path' exists, creating it (recursively) if necessary.
// Returns 0 on success, -1 if the directory cannot be created or accessed.
int results_writer_ensure_directory(const char *path);

// Join 'dir' and 'file' into 'dest', inserting a path separator when needed and
// ensuring the composed path fits within 'dest_size'.
// Returns 0 on success, -1 when the destination buffer would overflow or arguments are invalid.
int results_writer_join_path(char *dest, size_t dest_size, const char *dir, const char *file);

// Extract the matrix stem (filename without directory and extension) from 'matrix_path'
// into 'dest', guaranteeing null termination as long as the buffer is large enough.
// Returns 0 on success, -1 on invalid arguments or insufficient buffer size.
int results_writer_matrix_stem(const char *matrix_path, char *dest, size_t dest_size);

// Build a results file path inside 'dest' using 'output_dir', 'prefix', and the matrix stem
// computed from 'matrix_path', ensuring the final string is null-terminated and fits.
// Returns 0 on success, -1 if any argument is invalid or the destination is too small.
int results_writer_build_results_path(char *dest, size_t dest_size, const char *output_dir,
                                      const char *prefix, const char *matrix_path);

#endif /* RESULTS_WRITER_H */

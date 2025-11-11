#ifndef RESULTS_WRITER_H
#define RESULTS_WRITER_H

#include <stddef.h>

typedef enum {
    RESULTS_WRITER_OK = 0,
    RESULTS_WRITER_IO_ERROR = -1,
    RESULTS_WRITER_MEMORY_ERROR = -2,
    RESULTS_WRITER_INVALID_ARGS = -3
} results_writer_status;

results_writer_status append_times_column(const char *filename,
                                          const char *column_name,
                                          const double *values,
                                          size_t count);

int results_writer_ensure_directory(const char *path);

int results_writer_join_path(char *dest,
                             size_t dest_size,
                             const char *dir,
                             const char *file);

#endif /* RESULTS_WRITER_H */

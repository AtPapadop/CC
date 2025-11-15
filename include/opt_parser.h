#ifndef OPT_PARSER_H
#define OPT_PARSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int *values;
    size_t size;
    size_t capacity;
} OptIntList;

void opt_int_list_init(OptIntList *list);
void opt_int_list_free(OptIntList *list);
int opt_int_list_append(OptIntList *list, int value);
void opt_int_list_sort_unique(OptIntList *list);

int opt_parse_positive_int(const char *text, int *out);
int opt_parse_range_list(const char *spec, OptIntList *list, const char *label);

#ifdef __cplusplus
}
#endif

#endif /* OPT_PARSER_H */

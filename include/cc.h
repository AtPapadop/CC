#ifndef CC_H
#define CC_H

#include "graph.h"
#include <stdint.h>

// Sequential connected components algorithm using label propagation
// Non optimal for sequential execution but simple to parallelize later
void compute_connected_components(const CSRGraph *G, int32_t *labels);

// Connected components algorithm using BFS
// This is viable only for sequential execution
void compute_connected_components_bfs(const CSRGraph *G, int32_t *labels);

// Parallel connected components algorithm using OpenMP
// Identical to the label propagation version but using OpenMP for loop parallelism
void compute_connected_components_omp(const CSRGraph *G, int32_t *labels);

// Parallel connected components algorithm using OpenCilk
// Identical to the label propagation version but using Cilk for loop parallelism
void compute_connected_components_cilk(const CSRGraph *G, int32_t *labels);

// Parallel connected components algorithm using pthreads
// More complex implementation using pthreads for parallelism
void compute_connected_components_pthreads(const CSRGraph *G, int32_t *labels, int num_threads);

// Count the number of unique labels in the labels array
// Using label propagation, we know that labels are in the range [0, n-1]
int32_t count_unique_labels(const int32_t *labels, int32_t n);

#endif

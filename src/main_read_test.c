#include <stdio.h>
#include <inttypes.h>
#include "graph.h"

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s path/to/graph.mtx\n", argv[0]);
    return 1;
  }

  CSRGraph G;
  int rc = load_csr_from_mtx(argv[1], 1, 1, &G);
  if (rc != 0)
  {
    fprintf(stderr, "Error: load_csr_from_mtx failed (%d)\n", rc);
    return rc;
  }

  printf("Loaded graph: n=%d, m=%" PRId64 "\n", G.n, G.m);
  for (int i = 0; i < (G.n < 5 ? G.n : 5); ++i)
  {
    printf("vertex %d:", i);
    for (int64_t p = G.row_ptr[i]; p < G.row_ptr[i + 1]; ++p)
      printf(" %d", G.col_idx[p]);
    printf("\n");
  }

  free_csr(&G);
  return 0;
}

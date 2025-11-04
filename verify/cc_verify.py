import sys
import numpy as np
import networkx as nx
from scipy.io import mmread

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <matrix-market-file>")
    sys.exit(1)

path = sys.argv[1]
A = mmread(path).tocsc()

# Symmetrize the graph
A = ((A + A.T) > 0).astype(int)

G = nx.from_scipy_sparse_array(A)

components = list(nx.connected_components(G))
num_components = len(components)
print(f"Number of connected components: {num_components}")

# Build label array
n = A.shape[0]
labels = np.empty(n, dtype=int)
for label, component in enumerate(components):
    for node in component:
        labels[node] = label
        
np.savetxt("python_labels.txt", labels, fmt="%d")
print("Labels saved to python_labels.txt")
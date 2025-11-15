#!/bin/bash

# This script runs the connected components tests and verifies their correctness.
# Only command line argument is the input matrix in .mtx or .mat format and the total number of runs for averaging.

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <input_matrix.mtx/mat> <number_of_runs> <chunk_size> <output_folder>"
    exit 1
fi

# Create output folder if it doesn't exist
OUTPUT_FOLDER=$4
mkdir -p $OUTPUT_FOLDER

INPUT_MATRIX=$1
RUNS=$2
CHUNK_SIZE=$3
echo "Starting connected components tests verification..."
echo "Running tests on input file: $INPUT_MATRIX"
echo "Number of runs for averaging: $RUNS"
echo "Running bfs test..."
./bin/cc $INPUT_MATRIX --algorithm bfs --runs $RUNS -o $OUTPUT_FOLDER -c $CHUNK_SIZE
echo ""

echo "Starting OpenMP tests with varying thread counts..."
for t in 1 2 4 6 8 10 12 14 16
do
    echo "Running openmp test with $t thread(s)..."
    ./bin/cc $INPUT_MATRIX --threads $t --runs $RUNS -o $OUTPUT_FOLDER -c $CHUNK_SIZE
    echo ""
done

echo "Starting Cilk tests with varying worker counts..."
for t in 1 2 4 6 8 10 12 14 16
do
    echo "Running cilk test with $t worker(s)..."
    CILK_NWORKERS=$t ./bin/cc_cilk $INPUT_MATRIX --runs $RUNS -o $OUTPUT_FOLDER -c $CHUNK_SIZE
    echo ""
done

echo "Starting Pthreads tests with varying thread counts..."
for t in 1 2 4 6 8 10 12 14 16
do
    echo "Running pthreads test with $t thread(s)..."
    ./bin/cc_pthreads $INPUT_MATRIX --threads $t --runs $RUNS -o $OUTPUT_FOLDER -c $CHUNK_SIZE
    echo ""
done

echo "Connected components tests verification completed."
echo "Results are stored in the folder: $OUTPUT_FOLDER"
#!/bin/bash
#SBATCH --job-name=graph-bench
#SBATCH --partition=Centaurus
#SBATCH --time=00:20:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
#SBATCH --mem=20GB
#SBATCH --cpus-per-task=8

set -euo pipefail

mkdir -p logs results

echo "=== Building programs ==="
make clean
make

TRIALS=3

run_trials () {
local label="$1"
local bin="$2"
local node="$3"
local depth="$4"

echo
echo "[$label] node="$node" depth=$depth"

for t in $(seq 1 "$TRIALS"); do
echo "  trial $t:"
srun --cpu-bind=cores "$bin" "$node" "$depth" | tee -a "results/${label}_d${depth}.txt"
done
}

echo
echo "===== REQUIRED CASE: Tom Hanks / 4 ====="
run_trials "SEQ" ./seq_client "Tom Hanks" 4
run_trials "PAR" ./level_client "Tom Hanks" 4

echo
echo "===== ADDITIONAL TESTS ====="
run_trials "SEQ" ./seq_client "Damson Idris" 2
run_trials "PAR" ./level_client "Damson Idris" 2

echo
echo "Benchmark complete."

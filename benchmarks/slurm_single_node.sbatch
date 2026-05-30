#!/usr/bin/env bash
#SBATCH --job-name=spm_single_bench
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --nodelist=node01
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --time=00:20:00
#SBATCH --output=slurm_single_%j.out
#SBATCH --error=slurm_single_%j.err

set -Eeuo pipefail

cd "${SLURM_SUBMIT_DIR:-$PWD}"

export PROJECT_DIR="${SLURM_SUBMIT_DIR:-$PWD}"
LOCAL_TMP="${SLURM_TMPDIR:-${TMPDIR:-/tmp}}"
RUN_ROOT="$LOCAL_TMP/$USER/spmRun"
export RUN_DIR="$RUN_ROOT/${SLURM_JOB_ID:-localRun}"
export RESULTS_DIR="$PROJECT_DIR/benchmark_results"
export DATA_DIR="$RUN_DIR/data"
export TMP_BASE="$RUN_DIR/tmp"

mkdir -p "$RESULTS_DIR" "$DATA_DIR" "$TMP_BASE"

echo "SLURM_JOB_ID=${SLURM_JOB_ID:-}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST:-}"
echo "PROJECT_DIR=$PROJECT_DIR"
echo "RUN_DIR=$RUN_DIR"
echo "RESULTS_DIR=$RESULTS_DIR"
echo "DATA_DIR=$DATA_DIR"
echo "TMP_BASE=$TMP_BASE"
if command -v scontrol >/dev/null 2>&1; then
    scontrol show hostnames "${SLURM_JOB_NODELIST:-}"
fi

export THREAD_LIST="${THREAD_LIST:-1 2 4 8 16 32}"
export BENCHMARK_CASES="${BENCHMARK_CASES:-manySmall50M:50000000:64}"
export PAYLOAD_MAX_BUILD="${PAYLOAD_MAX_BUILD:-4096}"
export CHUNK_MB="${CHUNK_MB:-64}"
export MERGE_FAN="${MERGE_FAN:-8}"
export TRIALS="${TRIALS:-1}"
export VERIFY="${VERIFY:-0}"
export RUN_OMP="${RUN_OMP:-1}"
export RUN_FF="${RUN_FF:-0}"
export RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-180}"

./benchmarks/single_node.sh
python3 benchmarks/analyze.py --results-dir benchmark_results

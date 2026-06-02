#!/usr/bin/env bash
#SBATCH --job-name=spm_tune_single
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --nodelist=node01
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --time=00:10:00
#SBATCH --output=slurm_tune_single_%j.out
#SBATCH --error=slurm_tune_single_%j.err

set -Eeuo pipefail

cd "${SLURM_SUBMIT_DIR:-$PWD}"

export PROJECT_DIR="${SLURM_SUBMIT_DIR:-$PWD}"
LOCAL_TMP="${SLURM_TMPDIR:-${TMPDIR:-/tmp}}"
RUN_ROOT="$LOCAL_TMP/$USER/spmRun"
export RUN_DIR="$RUN_ROOT/${SLURM_JOB_ID:-localRun}"
export RESULTS_ROOT="$PROJECT_DIR/benchmark_results"
export BENCHMARK_RUN_ID="${SLURM_JOB_ID:-localRun}"
export RESULTS_DIR="$RESULTS_ROOT/run_$BENCHMARK_RUN_ID"
export LOG_DIR="$RESULTS_DIR/logs"
export DATA_DIR="$RUN_DIR/data"
export TMP_BASE="$RUN_DIR/tmp"

mkdir -p "$RESULTS_DIR" "$LOG_DIR" "$DATA_DIR" "$TMP_BASE"

echo "SLURM_JOB_ID=${SLURM_JOB_ID:-}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST:-}"
echo "PROJECT_DIR=$PROJECT_DIR"
echo "RUN_DIR=$RUN_DIR"
echo "RESULTS_ROOT=$RESULTS_ROOT"
echo "RESULTS_DIR=$RESULTS_DIR"
echo "LOG_DIR=$LOG_DIR"
echo "DATA_DIR=$DATA_DIR"
echo "TMP_BASE=$TMP_BASE"

export BENCHMARK_CASES="${BENCHMARK_CASES:-manySmall50M:50000000:64}"
export THREAD_LIST="${THREAD_LIST:-1 32}"
export CHUNK_MB_LIST="${CHUNK_MB_LIST:-32 64 128}"
export MERGE_FAN="${MERGE_FAN:-8}"
export TRIALS="${TRIALS:-1}"
export VERIFY="${VERIFY:-0}"
export OMP_PIPELINE="${OMP_PIPELINE:-1}"
export RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-180}"

./benchmarks/tune_single_node.sh
python3 benchmarks/analyze.py --results-dir "$RESULTS_DIR"

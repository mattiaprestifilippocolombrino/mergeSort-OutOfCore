#!/usr/bin/env bash
#SBATCH --job-name=spm_single_bench
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --time=00:20:00
#SBATCH --output=slurm_single_%j.out
#SBATCH --error=slurm_single_%j.err

set -Eeuo pipefail

cd "${SLURM_SUBMIT_DIR:-$PWD}"

export PROJECT_DIR="${SLURM_SUBMIT_DIR:-$PWD}"
SCRATCH_BASE="${SCRATCH_BASE:-/scratch/m.prestifilippoco}"
RUN_ROOT="$SCRATCH_BASE/spmRun/single"
export RUN_DIR="$RUN_ROOT/${SLURM_JOB_ID:-localRun}"
export RESULTS_ROOT="${RESULTS_ROOT:-$PROJECT_DIR/benchmark_results}"
export BENCHMARK_RUN_ID="${SLURM_JOB_ID:-localRun}"
export RESULTS_DIR="$RESULTS_ROOT/run_$BENCHMARK_RUN_ID"
export LOG_DIR="$RESULTS_DIR/logs"
export DATA_DIR="$RUN_DIR/data"
export TMP_BASE="$RUN_DIR/work"

mkdir -p "$RESULTS_DIR" "$LOG_DIR" "$DATA_DIR" "$TMP_BASE"
cleanup_scratch() {
    if [[ "${CLEAN_SCRATCH:-1}" == "1" && -n "${RUN_DIR:-}" && "$RUN_DIR" == /scratch/* ]]; then
        rm -rf "$RUN_DIR"
    fi
}
trap cleanup_scratch EXIT

echo "SLURM_JOB_ID=${SLURM_JOB_ID:-}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST:-}"
echo "PROJECT_DIR=$PROJECT_DIR"
echo "SCRATCH_BASE=$SCRATCH_BASE"
echo "RUN_DIR=$RUN_DIR"
echo "RESULTS_ROOT=$RESULTS_ROOT"
echo "RESULTS_DIR=$RESULTS_DIR"
echo "LOG_DIR=$LOG_DIR"
echo "DATA_DIR=$DATA_DIR"
echo "TMP_BASE=$TMP_BASE"
echo "CLEAN_SCRATCH=${CLEAN_SCRATCH:-1}"
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
export RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-900}"

./benchmarks/single_node.sh
python3 benchmarks/analyze.py --results-dir "$RESULTS_DIR"

#!/usr/bin/env bash
#SBATCH --job-name=spm_mpi_bench
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --time=00:20:00
#SBATCH --output=slurm_mpi_%j.out
#SBATCH --error=slurm_mpi_%j.err

set -Eeuo pipefail

cd "${SLURM_SUBMIT_DIR:-$PWD}"

export PROJECT_DIR="${SLURM_SUBMIT_DIR:-$PWD}"
SCRATCH_BASE="${SCRATCH_BASE:-/scratch/m.prestifilippoco}"
RUN_ROOT="$SCRATCH_BASE/spmRun/mpi"
export RUN_DIR="$RUN_ROOT/${SLURM_JOB_ID:-localRun}"
export RESULTS_ROOT="${RESULTS_ROOT:-$PROJECT_DIR/benchmark_results}"
export BENCHMARK_RUN_ID="${SLURM_JOB_ID:-localRun}"
export RESULTS_DIR="$RESULTS_ROOT/run_$BENCHMARK_RUN_ID"
export LOG_DIR="$RESULTS_DIR/logs"
export DATA_DIR="$RUN_DIR/data"
export TMP_BASE="$RUN_DIR/work"

mkdir -p "$RESULTS_DIR" "$LOG_DIR" "$DATA_DIR" "$TMP_BASE"
if [[ -n "${SLURM_JOB_ID:-}" && -n "${SLURM_JOB_NUM_NODES:-}" ]] && command -v srun >/dev/null 2>&1; then
    srun -N "$SLURM_JOB_NUM_NODES" -n "$SLURM_JOB_NUM_NODES" --ntasks-per-node=1 --cpu-bind=none \
        bash -c '
            set -Eeuo pipefail
            scratch_base="$1"
            data_dir="$2"
            tmp_base="$3"
            mkdir -p "$scratch_base" "$data_dir" "$tmp_base"
            test -w "$scratch_base"
        ' _ "$SCRATCH_BASE" "$DATA_DIR" "$TMP_BASE"
fi
cleanup_scratch() {
    if [[ "${CLEAN_SCRATCH:-1}" == "1" && -n "${RUN_DIR:-}" && "$RUN_DIR" == /scratch/* ]]; then
        if [[ -n "${SLURM_JOB_ID:-}" && -n "${SLURM_JOB_NUM_NODES:-}" ]] && command -v srun >/dev/null 2>&1; then
            srun -N "$SLURM_JOB_NUM_NODES" -n "$SLURM_JOB_NUM_NODES" --ntasks-per-node=1 --cpu-bind=none \
                rm -rf "$RUN_DIR" || true
        fi
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

export STRONG_NODES="${STRONG_NODES:-1 2}"
export RANKS_PER_NODE="${RANKS_PER_NODE:-1}"
export MPI_THREAD_LIST="${MPI_THREAD_LIST:-4 16}"
export BENCHMARK_CASES="${BENCHMARK_CASES:-manySmall20M:20000000:64}"
export WEAK_CASES="${WEAK_CASES:-weakSmall2500k:2500000:64}"
export PAYLOAD_MAX_BUILD="${PAYLOAD_MAX_BUILD:-4096}"
export CHUNK_MB="${CHUNK_MB:-64}"
export MERGE_FAN="${MERGE_FAN:-64}"
export TRIALS="${TRIALS:-1}"
export VERIFY="${VERIFY:-0}"
export RUN_STRONG="${RUN_STRONG:-1}"
export RUN_WEAK="${RUN_WEAK:-0}"

if [[ "$RUN_STRONG" == "1" ]]; then
    ./benchmarks/mpi_strong.sh
fi

if [[ "$RUN_WEAK" == "1" ]]; then
    ./benchmarks/mpi_weak.sh
fi

python3 benchmarks/analyze.py --results-dir "$RESULTS_DIR"

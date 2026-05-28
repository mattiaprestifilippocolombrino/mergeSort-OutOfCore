#!/usr/bin/env bash
#SBATCH --job-name=spm_mpi_bench
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --nodelist=node[01-08]
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --time=03:00:00
#SBATCH --output=slurm_mpi_%j.out
#SBATCH --error=slurm_mpi_%j.err

set -Eeuo pipefail

cd "${SLURM_SUBMIT_DIR:-$PWD}"
mkdir -p benchmark_results

echo "SLURM_JOB_ID=${SLURM_JOB_ID:-}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST:-}"
if command -v scontrol >/dev/null 2>&1; then
    scontrol show hostnames "${SLURM_JOB_NODELIST:-}"
fi

export STRONG_NODES="${STRONG_NODES:-1 2 4 8}"
export RANKS_PER_NODE="${RANKS_PER_NODE:-1}"
export MPI_THREAD_LIST="${MPI_THREAD_LIST:-1 2 4 8 16}"
export TRIALS="${TRIALS:-5}"
export VERIFY="${VERIFY:-1}"
export RUN_STRONG="${RUN_STRONG:-1}"
export RUN_WEAK="${RUN_WEAK:-1}"

if [[ "$RUN_STRONG" == "1" ]]; then
    ./benchmarks/mpi_strong.sh
fi

if [[ "$RUN_WEAK" == "1" ]]; then
    ./benchmarks/mpi_weak.sh
fi

python3 benchmarks/analyze.py --results-dir benchmark_results

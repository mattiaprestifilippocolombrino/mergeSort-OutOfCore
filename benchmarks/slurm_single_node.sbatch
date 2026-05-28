#!/usr/bin/env bash
#SBATCH --job-name=spm_single_bench
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --nodelist=node01
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --time=01:00:00
#SBATCH --output=slurm_single_%j.out
#SBATCH --error=slurm_single_%j.err

set -Eeuo pipefail

cd "${SLURM_SUBMIT_DIR:-$PWD}"
mkdir -p benchmark_results

echo "SLURM_JOB_ID=${SLURM_JOB_ID:-}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST:-}"
if command -v scontrol >/dev/null 2>&1; then
    scontrol show hostnames "${SLURM_JOB_NODELIST:-}"
fi

export THREAD_LIST="${THREAD_LIST:-1 2 4 8 16 32}"
export TRIALS="${TRIALS:-5}"
export VERIFY="${VERIFY:-1}"

./benchmarks/single_node.sh
python3 benchmarks/analyze.py --results-dir benchmark_results

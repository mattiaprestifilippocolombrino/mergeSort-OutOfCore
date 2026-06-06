#!/usr/bin/env bash
#SBATCH --job-name=spm_setup_scratch
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:02:00
#SBATCH --output=slurm_setup_scratch_%j.out
#SBATCH --error=slurm_setup_scratch_%j.err

set -Eeuo pipefail

cd "${SLURM_SUBMIT_DIR:-$PWD}"
./benchmarks/setup_scratch.sh

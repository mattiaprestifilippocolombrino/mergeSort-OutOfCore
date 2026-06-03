#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SCRATCH_BASE="${SCRATCH_BASE:-/scratch/m.prestifilippoco}"

make_scratch_tree() {
    local scratch_base="$1"
    mkdir -p \
        "$scratch_base/spmRun/single" \
        "$scratch_base/spmRun/mpi" \
        "$scratch_base/spmRun/tune"

    if [[ ! -w "$scratch_base" ]]; then
        echo "Scratch non scrivibile: $scratch_base" >&2
        exit 2
    fi
}

if [[ -z "${SLURM_JOB_ID:-}" ]]; then
    if command -v sbatch >/dev/null 2>&1; then
        echo "Questo nodo non deve creare /scratch direttamente; sottometto un job di setup."
        cd "$PROJECT_DIR"
        sbatch --export=ALL,SCRATCH_BASE="$SCRATCH_BASE" benchmarks/slurm_setup_scratch.sbatch
        exit 0
    fi

    echo "Esegui questo script dentro una allocazione Slurm, per esempio:" >&2
    echo "  salloc -N 1 -n 1 --time=00:02:00 ./benchmarks/setup_scratch.sh" >&2
    exit 2
fi

make_scratch_tree "$SCRATCH_BASE"

if [[ -n "${SLURM_JOB_NUM_NODES:-}" ]] && command -v srun >/dev/null 2>&1; then
    srun -N "$SLURM_JOB_NUM_NODES" -n "$SLURM_JOB_NUM_NODES" --ntasks-per-node=1 --cpu-bind=none \
        bash -c '
            set -Eeuo pipefail
            scratch_base="$1"
            mkdir -p \
                "$scratch_base/spmRun/single" \
                "$scratch_base/spmRun/mpi" \
                "$scratch_base/spmRun/tune"
            test -w "$scratch_base"
        ' _ "$SCRATCH_BASE"
fi

echo "Scratch pronta sui nodi allocati: $SCRATCH_BASE"

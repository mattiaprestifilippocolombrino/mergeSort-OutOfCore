#!/usr/bin/env bash

set -Eeuo pipefail

SCRATCH_BASE="${SCRATCH_BASE:-/scratch/m.prestifilippoco}"

make_scratch_tree() {
    local scratch_base="$1"
    mkdir -p \
        "$scratch_base/spmRun/slurm" \
        "$scratch_base/spmRun/results" \
        "$scratch_base/spmRun/single" \
        "$scratch_base/spmRun/mpi" \
        "$scratch_base/spmRun/tune"

    if [[ ! -w "$scratch_base" ]]; then
        echo "Scratch non scrivibile: $scratch_base" >&2
        exit 2
    fi
}

make_scratch_tree "$SCRATCH_BASE"

if [[ -n "${SLURM_JOB_ID:-}" && -n "${SLURM_JOB_NUM_NODES:-}" ]] && command -v srun >/dev/null 2>&1; then
    srun -N "$SLURM_JOB_NUM_NODES" -n "$SLURM_JOB_NUM_NODES" --ntasks-per-node=1 --cpu-bind=none \
        bash -c '
            set -Eeuo pipefail
            scratch_base="$1"
            mkdir -p \
                "$scratch_base/spmRun/slurm" \
                "$scratch_base/spmRun/results" \
                "$scratch_base/spmRun/single" \
                "$scratch_base/spmRun/mpi" \
                "$scratch_base/spmRun/tune"
            test -w "$scratch_base"
        ' _ "$SCRATCH_BASE"
fi

echo "Scratch pronta: $SCRATCH_BASE"
echo "Slurm output : $SCRATCH_BASE/spmRun/slurm"
echo "Risultati    : $SCRATCH_BASE/spmRun/results"

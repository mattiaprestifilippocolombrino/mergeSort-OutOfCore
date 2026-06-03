#!/bin/bash
#SBATCH --job-name=spm_sort_bench
#SBATCH --partition=normal
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --time=00:20:00
#SBATCH --output=slurm-%j.out
#SBATCH --error=slurm-%j.err

set -euo pipefail

# Benchmark iniziale semplice per spmcluster.
# Richiede 2 nodi per poter testare anche la versione MPI.
# Le versioni OpenMP/FastFlow usano solo 1 task, quindi girano su un nodo
# dentro la stessa allocazione SLURM.

PROJECT_DIR="${SLURM_SUBMIT_DIR:-$PWD}"
BUILD_DIR="$PROJECT_DIR/build_slurm"

RECORDS="${RECORDS:-1000000}"
PAYLOAD_MAX="${PAYLOAD_MAX:-256}"
CHUNK_MB="${CHUNK_MB:-128}"
MERGE_FAN="${MERGE_FAN:-16}"
THREADS="${SLURM_CPUS_PER_TASK:-16}"
MPI_RANKS="${SLURM_NTASKS:-2}"

TMP_BASE="${SLURM_TMPDIR:-${TMPDIR:-/tmp}}"
RUN_DIR="$TMP_BASE/spm_sort_${SLURM_JOB_ID:-manual}"

INPUT="$RUN_DIR/input.bin"
MPI_INPUT="$RUN_DIR/input_mpi.bin"
OUT_OMP="$RUN_DIR/output_omp.bin"
OUT_FF="$RUN_DIR/output_ff.bin"
OUT_MPI="$RUN_DIR/output_mpi.bin"

echo "=== SPM out-of-core sort benchmark ==="
echo "Job ID       : ${SLURM_JOB_ID:-manual}"
echo "Project dir  : $PROJECT_DIR"
echo "Build dir    : $BUILD_DIR"
echo "Run dir      : $RUN_DIR"
echo "Nodes        : ${SLURM_JOB_NUM_NODES:-unknown}"
echo "MPI ranks    : $MPI_RANKS"
echo "Threads/rank : $THREADS"
echo "Records      : $RECORDS"
echo "Payload max  : $PAYLOAD_MAX"
echo "Chunk MB     : $CHUNK_MB"
echo "Merge fan    : $MERGE_FAN"
echo

mkdir -p "$RUN_DIR"
trap 'rm -rf "$RUN_DIR"' EXIT

cd "$PROJECT_DIR"

echo "=== Environment ==="
hostname
date
command -v cmake || true
command -v mpicxx || true
command -v srun || true
echo

# Se il cluster usa Environment Modules, puoi decommentare/adattare queste righe:
# module load cmake
# module load openmpi

echo "=== Configure and build ==="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j "$THREADS"
echo

echo "=== Generate input ==="
srun -N 1 -n 1 -c "$THREADS" \
    "$BUILD_DIR/generate" "$INPUT" "$RECORDS" --payload-max "$PAYLOAD_MAX"
echo

echo "=== OpenMP version ==="
/usr/bin/time -p srun -N 1 -n 1 -c "$THREADS" \
    "$BUILD_DIR/omp_sort" "$INPUT" "$OUT_OMP" \
    --chunk-mb "$CHUNK_MB" \
    --threads "$THREADS" \
    --tmp-dir "$RUN_DIR" \
    --multipass-merge \
    --merge-fan "$MERGE_FAN"

"$BUILD_DIR/verify" "$INPUT" "$OUT_OMP"
rm -f "$OUT_OMP"
echo

if [[ -x "$BUILD_DIR/ff_sort" ]]; then
    echo "=== FastFlow version ==="
    /usr/bin/time -p srun -N 1 -n 1 -c "$THREADS" \
        "$BUILD_DIR/ff_sort" "$INPUT" "$OUT_FF" \
        --chunk-mb "$CHUNK_MB" \
        --workers "$THREADS" \
        --tmp-dir "$RUN_DIR" \
        --multipass-merge \
        --merge-fan "$MERGE_FAN"

    "$BUILD_DIR/verify" "$INPUT" "$OUT_FF"
    rm -f "$OUT_FF"
    echo
else
    echo "=== FastFlow version skipped ==="
    echo "ff_sort non e' stato compilato. Se vuoi testarlo, installa FastFlow o imposta FF_ROOT prima di cmake."
    echo
fi

echo "=== MPI + OpenMP version ==="
if [[ "${SLURM_JOB_NUM_NODES:-1}" -gt 1 && -n "${SLURM_JOB_ID:-}" && "$(command -v sbcast || true)" ]]; then
    echo "Broadcast input su /tmp locale dei nodi MPI..."
    srun -N "${SLURM_JOB_NUM_NODES:-2}" -n "${SLURM_JOB_NUM_NODES:-2}" --ntasks-per-node=1 \
        mkdir -p "$RUN_DIR"
    sbcast -f "$INPUT" "$MPI_INPUT"
else
    MPI_INPUT="$INPUT"
fi

/usr/bin/time -p srun --mpi=pmix -N "${SLURM_JOB_NUM_NODES:-2}" -n "$MPI_RANKS" -c "$THREADS" \
    "$BUILD_DIR/mpi_sort" "$MPI_INPUT" "$OUT_MPI" \
    --chunk-mb "$CHUNK_MB" \
    --threads "$THREADS" \
    --tmp-dir "$RUN_DIR" \
    --multipass-local-merge \
    --merge-fan "$MERGE_FAN"

"$BUILD_DIR/verify" "$MPI_INPUT" "$OUT_MPI"
rm -f "$OUT_MPI"
echo

echo "=== Done ==="
date

#!/usr/bin/env bash

set -Eeuo pipefail

MPI_THREAD_LIST="${MPI_THREAD_LIST:-1 4 8 16 32}"
RANKS_PER_NODE="${RANKS_PER_NODE:-1}"
TRIALS="${TRIALS:-1}"
VERIFY="${VERIFY:-0}"
PAYLOAD_MAX_BUILD="${PAYLOAD_MAX_BUILD:-4096}"
CHUNK_MB="${CHUNK_MB:-64}"
MERGE_FAN="${MERGE_FAN:-8}"
MPI_STRONG_TIME="${MPI_STRONG_TIME:-${SBATCH_TIME:-00:29:00}}"
MPI_WEAK_TIME="${MPI_WEAK_TIME:-00:03:00}"

STRONG_CASE="${STRONG_CASE:-manySmall200M:200000000:64}"
NODE_POINTS="${NODE_POINTS:-1 2 4 8}"

submit_mpi_job() {
    local mode="$1"
    local nodes="$2"
    local threads="$3"
    local sbatch_time="$MPI_STRONG_TIME"

    if [[ "$mode" == "strong" ]]; then
        sbatch_time="$MPI_STRONG_TIME"
        printf '[submit] MPI strong nodes=%s threads=%s time=%s chunk_mb=%s merge_fan=%s\n' \
            "$nodes" "$threads" "$sbatch_time" "$CHUNK_MB" "$MERGE_FAN" >&2
        RUN_STRONG=1 RUN_WEAK=0 \
        BENCHMARK_CASES="$STRONG_CASE" \
        STRONG_NODES="$nodes" \
        RANKS_PER_NODE="$RANKS_PER_NODE" \
        MPI_THREAD_LIST="$threads" \
        PAYLOAD_MAX_BUILD="$PAYLOAD_MAX_BUILD" \
        CHUNK_MB="$CHUNK_MB" \
        MERGE_FAN="$MERGE_FAN" \
        TRIALS="$TRIALS" \
        VERIFY="$VERIFY" \
        sbatch --nodes="$nodes" --time="$sbatch_time" benchmarks/slurm_mpi_scaling.sbatch
    else
        sbatch_time="$MPI_WEAK_TIME"
        printf '[submit] MPI weak nodes=%s threads=%s time=%s chunk_mb=%s merge_fan=%s\n' \
            "$nodes" "$threads" "$sbatch_time" "$CHUNK_MB" "$MERGE_FAN" >&2
        RUN_STRONG=0 RUN_WEAK=1 \
        STRONG_NODES="$nodes" \
        RANKS_PER_NODE="$RANKS_PER_NODE" \
        MPI_THREAD_LIST="$threads" \
        PAYLOAD_MAX_BUILD="$PAYLOAD_MAX_BUILD" \
        CHUNK_MB="$CHUNK_MB" \
        MERGE_FAN="$MERGE_FAN" \
        TRIALS="$TRIALS" \
        VERIFY="$VERIFY" \
        sbatch --nodes="$nodes" --time="$sbatch_time" benchmarks/slurm_mpi_scaling.sbatch
    fi
}

for nodes in $NODE_POINTS; do
    for threads in $MPI_THREAD_LIST; do
        submit_mpi_job strong "$nodes" "$threads"
    done
done

for nodes in $NODE_POINTS; do
    for threads in $MPI_THREAD_LIST; do
        submit_mpi_job weak "$nodes" "$threads"
    done
done

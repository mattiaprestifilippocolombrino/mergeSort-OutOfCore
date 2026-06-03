#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CSV="${1:-$RESULTS_DIR/single_node_tuning_raw.csv}"

validate_benchmark_config
prepare_build

CHUNK_MB_LIST="${CHUNK_MB_LIST:-64 128 256}"
MERGE_FAN_LIST="${MERGE_FAN_LIST:-16 32 64}"

write_csv_header "$CSV" \
    "suite,impl,merge_impl,case,trial,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,sort_s,merge_s,total_s,verified,log_file"

combo_count_chunk="$(wc -w <<<"$CHUNK_MB_LIST")"
combo_count_fan="$(wc -w <<<"$MERGE_FAN_LIST")"
combo_count=$((combo_count_chunk * combo_count_fan))

log "Tuning OpenMP pipeline merge: $combo_count combinazioni (chunk x fan); casi=$BENCHMARK_CASES; thread=$THREAD_LIST"

for chunk in $CHUNK_MB_LIST; do
    for fan in $MERGE_FAN_LIST; do
        log "Tuning CHUNK_MB=$chunk, MERGE_FAN=$fan"
        CHUNK_MB="$chunk" \
        MERGE_FAN="$fan" \
        LOG_TAG="c${chunk}_f${fan}" \
        RUN_OMP=1 \
        RUN_FF=0 \
        OMP_PIPELINE=1 \
        APPEND_RESULTS=1 \
        SKIP_BUILD=1 \
        ./benchmarks/single_node.sh "$CSV"
    done
done

log "CSV tuning scritto in $CSV"

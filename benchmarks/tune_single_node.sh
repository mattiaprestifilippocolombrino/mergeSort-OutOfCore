#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CSV="${1:-$RESULTS_DIR/single_node_tuning_raw.csv}"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    build_project
fi

CHUNK_MB_LIST="${CHUNK_MB_LIST:-32 64 128}"
MERGE_FAN="${MERGE_FAN:-8}"

write_csv_header "$CSV" \
    "suite,impl,merge_impl,case,trial,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,sort_s,merge_s,total_s,verified,log_file"

combo_count="$(wc -w <<<"$CHUNK_MB_LIST")"
log "Tuning OpenMP flat merge: $combo_count dimensioni chunk; casi=$BENCHMARK_CASES; thread=$THREAD_LIST"

for chunk in $CHUNK_MB_LIST; do
    log "Tuning CHUNK_MB=$chunk"
    CHUNK_MB="$chunk" \
    MERGE_FAN="$MERGE_FAN" \
    LOG_TAG="c${chunk}" \
    RUN_OMP=1 \
    RUN_FF=0 \
    APPEND_RESULTS=1 \
    SKIP_BUILD=1 \
    ./benchmarks/single_node.sh "$CSV"
done

log "CSV tuning scritto in $CSV"

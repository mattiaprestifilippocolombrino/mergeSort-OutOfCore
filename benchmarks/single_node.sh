#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CSV="${1:-$RESULTS_DIR/single_node_raw.csv}"
RUN_ROOT="$TMP_BASE/spm_single_node_bench_${SLURM_JOB_ID:-$$}"

mkdir -p "$RUN_ROOT"
trap 'rm -rf "$RUN_ROOT"' EXIT

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    build_project
fi

write_csv_header "$CSV" \
    "suite,impl,case,trial,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,sort_s,merge_s,total_s,verified,log_file"

impl_count=0
if [[ "${RUN_OMP:-1}" == "1" ]]; then
    impl_count=$((impl_count + 1))
fi
if [[ "${RUN_FF:-1}" == "1" && -x "$BUILD_DIR/ff_sort" ]]; then
    impl_count=$((impl_count + 1))
fi
if [[ "$impl_count" == "0" ]]; then
    log "Nessuna implementazione abilitata: imposta RUN_OMP=1 o RUN_FF=1."
    exit 1
fi
case_count="$(wc -w <<<"$BENCHMARK_CASES")"
thread_count="$(wc -w <<<"$THREAD_LIST")"
planned_runs=$((case_count * thread_count * TRIALS * impl_count))
log "Run single-node pianificate: $planned_runs ($case_count casi x $thread_count thread x $TRIALS trial x $impl_count implementazioni)"

run_impl() {
    local impl="$1"
    local case="$2"
    local input="$3"
    local records="$4"
    local payload="$5"
    local threads="$6"
    local trial="$7"

    local output="$RUN_ROOT/${impl}_${case}_t${threads}_i${trial}.bin"
    local log_file="$RESULTS_DIR/${impl}_${case}_t${threads}_i${trial}.log"

    if [[ "$impl" == "omp" ]]; then
        if ! run_and_capture_sort "$log_file" \
            run_single "$threads" "$BUILD_DIR/omp_sort" "$input" "$output" \
                --chunk-mb "$CHUNK_MB" \
                --threads "$threads" \
                --tmp-dir "$RUN_ROOT" \
                --merge-fan "$MERGE_FAN"; then
            rm -f "$output"
            return 1
        fi
    else
        if ! run_and_capture_sort "$log_file" \
            run_single "$threads" "$BUILD_DIR/ff_sort" "$input" "$output" \
                --chunk-mb "$CHUNK_MB" \
                --workers "$threads" \
                --tmp-dir "$RUN_ROOT" \
                --merge-fan "$MERGE_FAN"; then
            rm -f "$output"
            return 1
        fi
    fi

    if ! verify_output "$input" "$output"; then
        rm -f "$output"
        return 1
    fi
    rm -f "$output"

    local generated_runs sort_s merge_s total_s
    generated_runs="$(extract_generated_runs <"$log_file")"
    sort_s="$(extract_seconds "Fase 1" <"$log_file")"
    merge_s="$(extract_seconds "Fase 2" <"$log_file")"
    total_s="$(extract_seconds "Totale" <"$log_file")"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "single_node" "$impl" "$case" "$trial" "$records" "$payload" \
        "$threads" "$CHUNK_MB" "$MERGE_FAN" "$generated_runs" "$sort_s" "$merge_s" "$total_s" \
        "$VERIFY" "$log_file" >>"$CSV"
}

for spec in $BENCHMARK_CASES; do
    name="$(case_name "$spec")"
    records="$(case_records "$spec")"
    payload="$(case_payload "$spec")"
    input="$(ensure_dataset "$name" "$records" "$payload")"

    for trial in $(seq 1 "$TRIALS"); do
        for threads in $THREAD_LIST; do
            if [[ "${RUN_OMP:-1}" == "1" ]]; then
                log "OpenMP case=$name trial=$trial threads=$threads"
                if ! run_impl "omp" "$name" "$input" "$records" "$payload" "$threads" "$trial"; then
                    log "OpenMP fallito: case=$name trial=$trial threads=$threads. Vedi log in $RESULTS_DIR."
                fi
            fi

            if [[ "${RUN_FF:-1}" == "1" && -x "$BUILD_DIR/ff_sort" ]]; then
                log "FastFlow case=$name trial=$trial workers=$threads"
                if ! run_impl "ff" "$name" "$input" "$records" "$payload" "$threads" "$trial"; then
                    log "FastFlow fallito: case=$name trial=$trial workers=$threads. Vedi log in $RESULTS_DIR."
                fi
            elif [[ "${RUN_FF:-1}" == "1" && "$trial" == "1" && "$threads" == "$(awk '{print $1; exit}' <<<"$THREAD_LIST")" ]]; then
                log "ff_sort non presente: salto FastFlow. Imposta FF_ROOT e ricompila per includerlo."
            fi
        done
    done
done

log "CSV scritto in $CSV"

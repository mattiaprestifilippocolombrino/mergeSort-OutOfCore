#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

BENCHMARK_RUN_ID="${BENCHMARK_RUN_ID:-${SLURM_JOB_ID:-$(date +%Y%m%d_%H%M%S)_$$}}"
SCRATCH_BASE="${SCRATCH_BASE:-/scratch/m.prestifilippoco}"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build_bench}"
RESULTS_ROOT="${RESULTS_ROOT:-$PROJECT_DIR/benchmark_results}"
TMP_BASE="${TMP_BASE:-$SCRATCH_BASE/spm_benchmark_$BENCHMARK_RUN_ID/work}"
RESULTS_DIR="${RESULTS_DIR:-$RESULTS_ROOT/run_$BENCHMARK_RUN_ID}"
LOG_DIR="${LOG_DIR:-$RESULTS_DIR/logs}"
DATA_DIR="${DATA_DIR:-$TMP_BASE/spm_benchmark_data}"

PAYLOAD_MAX_BUILD="${PAYLOAD_MAX_BUILD:-4096}"
CHUNK_MB="${CHUNK_MB:-64}"
MERGE_FAN="${MERGE_FAN:-8}"
TRIALS="${TRIALS:-1}"
VERIFY="${VERIFY:-0}"
SEED="${SEED:-42}"
RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-0}"

# Two intentionally different regimes:
# - many short records: stresses comparisons, indexing, task scheduling;
# - fewer large records: stresses I/O bandwidth and payload movement.
BENCHMARK_CASES="${BENCHMARK_CASES:-manySmall50M:50000000:64}"

THREAD_LIST="${THREAD_LIST:-1 2 4 8 16 32}"
MPI_THREAD_LIST="${MPI_THREAD_LIST:-1 4 8 16 32}"
STRONG_NODES="${STRONG_NODES:-1 2 4 8}"
RANKS_PER_NODE="${RANKS_PER_NODE:-1}"
WEAK_PAYLOAD_MAX="${WEAK_PAYLOAD_MAX:-64}"
WEAK_TIME_BUDGET_SECONDS="${WEAK_TIME_BUDGET_SECONDS:-180}"
WEAK_PROBE_CHUNKS_PER_RANK="${WEAK_PROBE_CHUNKS_PER_RANK:-$MERGE_FAN}"

log() {
    printf '[bench] %s\n' "$*" >&2
}

prepare_storage_dirs() {
    mkdir -p "$RESULTS_ROOT" "$RESULTS_DIR" "$LOG_DIR" "$DATA_DIR" "$TMP_BASE"

    if [[ ! -d "$TMP_BASE" || ! -w "$TMP_BASE" ]]; then
        log "TMP_BASE non scrivibile: $TMP_BASE"
        log "Su cluster imposta SCRATCH_BASE=/scratch/m.prestifilippoco oppure TMP_BASE su scratch locale del nodo."
        return 2
    fi

    if [[ "$TMP_BASE" != /scratch/* ]]; then
        log "[WARN] TMP_BASE non e' sotto /scratch: $TMP_BASE"
        log "[WARN] Per misure finali HPC usa scratch locale del nodo, non filesystem condiviso."
    fi
    if [[ "$DATA_DIR" != /scratch/* ]]; then
        log "[WARN] DATA_DIR non e' sotto /scratch: $DATA_DIR"
        log "[WARN] Evita dataset grandi su home NFS durante benchmark HPC."
    fi
}

prepare_storage_dirs

require_uint() {
    local name="$1"
    local value="$2"
    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
        log "$name deve essere un intero non negativo, valore ricevuto: $value"
        return 2
    fi
}

require_positive_uint() {
    local name="$1"
    local value="$2"
    require_uint "$name" "$value"
    if (( value <= 0 )); then
        log "$name deve essere > 0, valore ricevuto: $value"
        return 2
    fi
}

validate_benchmark_config() {
    require_positive_uint "PAYLOAD_MAX_BUILD" "$PAYLOAD_MAX_BUILD"
    require_positive_uint "CHUNK_MB" "$CHUNK_MB"
    require_positive_uint "MERGE_FAN" "$MERGE_FAN"
    if (( MERGE_FAN < 2 )); then
        log "MERGE_FAN deve essere >= 2, valore ricevuto: $MERGE_FAN"
        return 2
    fi
    require_positive_uint "TRIALS" "$TRIALS"
    require_uint "RUN_TIMEOUT_SECONDS" "$RUN_TIMEOUT_SECONDS"
    require_positive_uint "WEAK_PAYLOAD_MAX" "$WEAK_PAYLOAD_MAX"
    require_positive_uint "WEAK_TIME_BUDGET_SECONDS" "$WEAK_TIME_BUDGET_SECONDS"
    require_positive_uint "WEAK_PROBE_CHUNKS_PER_RANK" "$WEAK_PROBE_CHUNKS_PER_RANK"

    local chunk_bytes=$((CHUNK_MB * 1024 * 1024))
    local min_record_bytes=$((PAYLOAD_MAX_BUILD + 12))
    if (( chunk_bytes < min_record_bytes )); then
        log "CHUNK_MB=$CHUNK_MB troppo piccolo per PAYLOAD_MAX_BUILD=$PAYLOAD_MAX_BUILD"
        log "Serve almeno $min_record_bytes byte per contenere un record massimo."
        return 2
    fi

    local threads
    for threads in $THREAD_LIST; do
        require_positive_uint "THREAD_LIST entry" "$threads"
    done

    local mpi_threads
    for mpi_threads in $MPI_THREAD_LIST; do
        require_positive_uint "MPI_THREAD_LIST entry" "$mpi_threads"
    done
}

build_project() {
    log "Configuro build Release in $BUILD_DIR con PAYLOAD_MAX=$PAYLOAD_MAX_BUILD"
    local cmake_args=(
        -S "$PROJECT_DIR"
        -B "$BUILD_DIR"
        -DCMAKE_BUILD_TYPE=Release
        -DPAYLOAD_MAX="$PAYLOAD_MAX_BUILD"
    )
    if [[ -n "${FF_ROOT:-}" ]]; then
        cmake_args+=("-DFF_ROOT=$FF_ROOT")
        log "FF_ROOT=$FF_ROOT"
    fi
    cmake "${cmake_args[@]}"
    cmake --build "$BUILD_DIR" -j "$(available_cpus)"
}

configured_payload_max() {
    local cache="$BUILD_DIR/CMakeCache.txt"
    if [[ ! -r "$cache" ]]; then
        return 1
    fi

    awk -F= '
        /^PAYLOAD_MAX(:[^=]*)?=/ {
            print $2
            found = 1
            exit
        }
        END {
            if (!found) exit 1
        }
    ' "$cache"
}

verify_skipped_build_payload() {
    local configured
    if ! configured="$(configured_payload_max)"; then
        log "SKIP_BUILD=1 ma non trovo PAYLOAD_MAX in $BUILD_DIR/CMakeCache.txt."
        log "Per i test finali rimuovi SKIP_BUILD=1, oppure ricompila prima con PAYLOAD_MAX_BUILD=$PAYLOAD_MAX_BUILD."
        return 2
    fi

    require_positive_uint "PAYLOAD_MAX nel CMakeCache" "$configured"
    if (( configured < PAYLOAD_MAX_BUILD )); then
        log "SKIP_BUILD=1 non sicuro: build esistente con PAYLOAD_MAX=$configured, richiesto PAYLOAD_MAX_BUILD=$PAYLOAD_MAX_BUILD."
        log "Rimuovi SKIP_BUILD=1 per ricompilare, oppure usa un BUILD_DIR compilato con -DPAYLOAD_MAX=$PAYLOAD_MAX_BUILD."
        return 2
    fi

    if (( configured > PAYLOAD_MAX_BUILD )); then
        log "SKIP_BUILD=1: build con PAYLOAD_MAX=$configured compatibile con PAYLOAD_MAX_BUILD=$PAYLOAD_MAX_BUILD."
    else
        log "SKIP_BUILD=1: build gia' compatibile con PAYLOAD_MAX=$configured."
    fi
}

prepare_build() {
    if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
        verify_skipped_build_payload
    else
        build_project
    fi
}

available_cpus() {
    if [[ -n "${SLURM_CPUS_PER_TASK:-}" ]]; then
        printf '%s\n' "$SLURM_CPUS_PER_TASK"
    elif command -v nproc >/dev/null 2>&1; then
        nproc
    else
        printf '1\n'
    fi
}

run_single() {
    local cpus="$1"
    shift

    if [[ -n "${SLURM_JOB_ID:-}" ]]; then
        local node_list
        node_list="$(slurm_node_list_for 1)"
        local node_args=()
        if [[ -n "$node_list" ]]; then
            node_args=(--nodelist "$node_list")
        fi
        srun "${node_args[@]}" -N 1 -n 1 -c "$cpus" "$@"
    else
        "$@"
    fi
}

run_single_benchmark() {
    local cpus="$1"
    shift

    if (( RUN_TIMEOUT_SECONDS > 0 )); then
        if [[ -n "${SLURM_JOB_ID:-}" ]]; then
            timeout --kill-after=10 "$RUN_TIMEOUT_SECONDS" \
                srun -N 1 -n 1 -c "$cpus" "$@"
        else
            timeout --kill-after=10 "$RUN_TIMEOUT_SECONDS" "$@"
        fi
    else
        run_single "$cpus" "$@"
    fi
}

slurm_node_list_for() {
    local nodes="$1"
    if [[ -n "${SLURM_JOB_NODELIST:-}" ]] && command -v scontrol >/dev/null 2>&1; then
        scontrol show hostnames "$SLURM_JOB_NODELIST" | head -n "$nodes" | paste -sd, -
    fi
}

run_mpi() {
    local nodes="$1"
    local ranks="$2"
    local threads="$3"
    shift 3

    export OMP_NUM_THREADS="$threads"
    export OMP_PLACES="${OMP_PLACES:-cores}"
    export OMP_PROC_BIND="${OMP_PROC_BIND:-spread}"

    if [[ -n "${SLURM_JOB_ID:-}" ]]; then
        local node_list
        node_list="$(slurm_node_list_for "$nodes")"
        local node_args=()
        if [[ -n "$node_list" ]]; then
            node_args=(--nodelist "$node_list")
        fi
        srun --mpi=pmix "${node_args[@]}" --cpu-bind="${SLURM_CPU_BIND_OPT:-cores}" -N "$nodes" -n "$ranks" --ntasks-per-node "${RANKS_PER_NODE:-1}" -c "$threads" "$@"
    elif command -v mpirun >/dev/null 2>&1; then
        mpirun --oversubscribe -n "$ranks" "$@"
    else
        log "mpirun non trovato: impossibile eseguire benchmark MPI fuori da SLURM"
        return 127
    fi
}

stage_mpi_input() {
    local src="$1"
    local nodes="$2"

    if [[ -z "${SLURM_JOB_ID:-}" ]]; then
        printf '%s\n' "$src"
        return 0
    fi

    local stage_dir="$TMP_BASE/mpi_input"
    local dst="$stage_dir/$(basename "$src")"

    log "Stage MPI input su storage locale: nodes=$nodes src=$src dst=$dst"
    local node_list
    node_list="$(slurm_node_list_for "$nodes")"
    local node_args=()
    if [[ -n "$node_list" ]]; then
        node_args=(--nodelist "$node_list")
    fi

    if command -v sbcast >/dev/null 2>&1; then
        local setup_nodes="$nodes"
        local setup_node_args=("${node_args[@]}")
        if [[ -n "${SLURM_JOB_NUM_NODES:-}" && "${SLURM_JOB_NUM_NODES:-0}" -gt "$nodes" ]]; then
            setup_nodes="$SLURM_JOB_NUM_NODES"
            setup_node_args=()
        fi
        srun "${setup_node_args[@]}" -N "$setup_nodes" -n "$setup_nodes" --ntasks-per-node 1 --cpu-bind=none \
            mkdir -p "$stage_dir"
        sbcast -f "$src" "$dst"
    else
        srun "${node_args[@]}" -N "$nodes" -n "$nodes" --ntasks-per-node 1 --cpu-bind=none \
            mkdir -p "$stage_dir"
        log "[WARN] sbcast non trovato: uso cp via srun, funziona solo se src e' visibile dai nodi."
        srun "${node_args[@]}" -N "$nodes" -n "$nodes" --ntasks-per-node 1 --cpu-bind=none \
            bash -c '
                set -Eeuo pipefail
                dst="$1"
                src="$2"
                if [[ ! -s "$dst" || "$src" -nt "$dst" ]]; then
                    cp -f "$src" "$dst"
                fi
            ' _ "$dst" "$src"
    fi

    printf '%s\n' "$dst"
}

case_name() {
    cut -d: -f1 <<<"$1"
}

case_records() {
    cut -d: -f2 <<<"$1"
}

case_payload() {
    cut -d: -f3 <<<"$1"
}

dataset_path() {
    local name="$1"
    local records="$2"
    local payload="$3"
    printf '%s/%s_n%s_p%s.bin\n' "$DATA_DIR" "$name" "$records" "$payload"
}

ensure_dataset() {
    local name="$1"
    local records="$2"
    local payload="$3"
    local path
    path="$(dataset_path "$name" "$records" "$payload")"
    local marker="$path.ok"
    local expected_marker="records=$records payload_max=$payload seed=$SEED payload_build=$PAYLOAD_MAX_BUILD"

    if [[ -s "$path" && -r "$marker" && "$(cat "$marker")" == "$expected_marker" ]]; then
        log "Dataset gia' presente: $path"
        printf '%s\n' "$path"
        return 0
    fi

    if [[ -e "$path" || -e "$marker" ]]; then
        log "Dataset esistente incompleto o con marker non valido, rigenero: $path"
        rm -f "$path" "$marker"
    fi

    if (( payload > PAYLOAD_MAX_BUILD )); then
        log "payload=$payload supera PAYLOAD_MAX_BUILD=$PAYLOAD_MAX_BUILD"
        return 2
    fi

    log "Genero dataset $name: N=$records payload_max=$payload"
    local tmp_path="$path.tmp.${SLURM_JOB_ID:-$$}"
    rm -f "$tmp_path"
    run_single "$(available_cpus)" "$BUILD_DIR/generate" "$tmp_path" "$records" \
        --payload-max "$payload" \
        --seed "$SEED" >/dev/null
    mv -f "$tmp_path" "$path"
    printf '%s\n' "$expected_marker" >"$marker"
    printf '%s\n' "$path"
}

extract_seconds() {
    local label="$1"
    awk -v label="$label" '
        index($0, label) {
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) {
                    value = $i
                }
            }
        }
        END {
            if (value == "") {
                print "nan"
            } else {
                print value
            }
        }
    '
}

extract_generated_runs() {
    awk '
        index($0, "Fase 1") {
            for (i = 2; i <= NF; ++i) {
                if ($i == "run" && $(i - 1) ~ /^[0-9]+$/) {
                    value = $(i - 1)
                }
            }
        }
        END {
            if (value == "") {
                print "0"
            } else {
                print value
            }
        }
    '
}

run_and_capture_sort() {
    local out_log="$1"
    shift
    mkdir -p "$(dirname "$out_log")"
    "$@" >"$out_log" 2>&1
}

write_csv_header() {
    local file="$1"
    local header="$2"
    mkdir -p "$(dirname "$file")"
    if [[ "${APPEND_RESULTS:-0}" == "1" && -s "$file" ]]; then
        local existing_header
        existing_header="$(head -n 1 "$file")"
        if [[ "$existing_header" != "$header" ]]; then
            log "Header CSV incompatibile in $file"
            log "Atteso   : $header"
            log "Trovato  : $existing_header"
            log "Rimuovi il CSV o usa un RESULTS_DIR nuovo prima di appendere risultati."
            return 2
        fi
        return
    else
        printf '%s\n' "$header" >"$file"
    fi
}

verify_output() {
    local input="$1"
    local output="$2"
    if [[ ! -s "$output" ]]; then
        log "Output mancante o vuoto: $output"
        return 1
    fi

    local input_size output_size
    input_size="$(stat -c '%s' "$input")"
    output_size="$(stat -c '%s' "$output")"
    if [[ "$input_size" != "$output_size" ]]; then
        log "Dimensione output errata: input=$input_size byte output=$output_size byte file=$output"
        return 1
    fi

    if [[ "$VERIFY" == "1" ]]; then
        "$BUILD_DIR/verify" "$input" "$output" >/dev/null
    fi
}

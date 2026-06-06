# Guida tuning OpenMP: chunk size e merge fan

Questa guida serve solo a scegliere `CHUNK_MB` e `MERGE_FAN` usando la versione
OpenMP. Non lancia FastFlow e non lancia MPI: il tuning deve essere breve,
ripetibile e separato dalla campagna finale.

## Regole

- Usa solo `benchmarks/slurm_tune_single_node.sbatch`.
- Tieni ogni job sotto i 30 minuti.
- Usa `/scratch` per dati e file temporanei; gli script lo fanno tramite
  `TMP_BASE` e `DATA_DIR`.
- Lascia `VERIFY=0` durante il tuning; fai la verifica in un job piccolo
  separato.
- Non usare `SKIP_BUILD=1` quando cambi `PAYLOAD_MAX_BUILD`.
- `./benchmarks/setup_scratch.sh` sottomette un micro-job di verifica quando lo
  lanci dal login node; i job di benchmark creano comunque la propria directory
  scratch all'avvio.

## Dataset tuning

Default consigliato:

```bash
BENCHMARK_CASES="tuneSmall10M:10000000:64"
THREAD_LIST="1 32"
PAYLOAD_MAX_BUILD=4096
TRIALS=1
VERIFY=0
```

Questo misura sia la baseline a 1 thread sia il caso parallelo a 32 thread,
senza mischiare troppe dimensioni in un solo job.

## Job finale: parametri fissati

```bash
cd ~/spmProject
./benchmarks/setup_scratch.sh
BENCHMARK_CASES="tuneSmall10M:10000000:64" \
THREAD_LIST="1 32" \
CHUNK_MB_LIST="64" \
MERGE_FAN_LIST="8" \
PAYLOAD_MAX_BUILD=4096 TRIALS=1 VERIFY=0 \
sbatch --time=00:15:00 benchmarks/slurm_tune_single_node.sbatch
```

## Lettura risultati

Ogni job produce una directory:

```text
benchmark_results/run_<jobid>/
```

I file principali sono:

```text
single_node_tuning_raw.csv
single_node_tuning_summary.csv
plots/
logs/
```

Per confrontare piu' job:

```bash
ls -td benchmark_results/run_*
cat benchmark_results/run_*/single_node_tuning_summary.csv
```

Scegli la coppia `CHUNK_MB`, `MERGE_FAN` con `avg_total_s` piu' basso nel caso
`threads=32`, controllando anche `avg_merge_s`: se due configurazioni hanno
tempi simili, preferisci quella con `MERGE_FAN` piu' basso per ridurre file
descriptor e memoria per merge task.

## Applicazione alla campagna finale

La campagna consegna usa attualmente:

```bash
CHUNK_MB=64
MERGE_FAN=8
```

Se il tuning mostra una configurazione migliore, aggiorna solo i comandi delle
guide finali e gli export negli sbatch, poi rilancia una run piccola con
`VERIFY=1`.

# Benchmark suite

Questa cartella contiene gli script per misurare speedup, efficiency, strong scaling e weak scaling del progetto.

La struttura e' ispirata a `benchmarks_others`: domini separati, CSV riassuntivi, payload diversi, thread sweep e scaling MPI. La differenza e' che qui i job sono piu' piccoli e usano parametri fissati dopo un tuning breve, per evitare esecuzioni troppo lunghe sullo spmcluster.

### Latenza Nascosta con Pipeline Asincrona (Nuova Versione Standard)
La versione standard usa il merge a **pipeline asincrona**: ogni thread OMP, FastFlow o MPI usa una coda a doppio buffer (`pipelineMergePass()`) in cui un Thread asincrono scrive su disco sequenzialmente chunk da 32MB mentre la CPU scorre l'heap in RAM e il disco carica blocchi grandi in memoria. In questa architettura il disco è costantemente in funzione alla sua banda sequenziale massima, nascondendo la lentezza dei flush. Il multi-pass `legacy` è disabilitato e conservato solo per scopo comparativo impostando la variabile ambiente associata. dimensioni di chunk su `manySmall50M`:

```text
slurm_tune_single_node.sbatch
```

che produce:

```text
benchmark_results/run_<jobid>/single_node_tuning_raw.csv
benchmark_results/run_<jobid>/single_node_tuning_summary.csv
```

Configurazione:

```bash
CHUNK_MB_LIST="32 64 128"
THREAD_LIST="1 32"
```

Si sceglie la configurazione con `avg_total_s` piu' basso a `threads=32`. Il default resta `CHUNK_MB=64`, che genera abbastanza run su file grandi senza creare troppi file temporanei.

## Campagna finale

### Single-node speedup

Caso principale:

```bash
manySmall50M:50000000:64
```

Thread:

```bash
1 2 4 8 16 32
```

Si misura separatamente:

- OpenMP;
- FastFlow.

Metriche:

```text
speedup = T_1 / T_p
efficiency = speedup / p
```

Il summary CSV include anche le stesse metriche calcolate sulle singole fasi:

```text
sort_speedup, sort_efficiency
merge_speedup, merge_efficiency
```

Questo serve a separare lo scaling della Fase 1, cioe' sort dei chunk, dallo
scaling della Fase 2, cioe' merge delle run. Nei grafici single-node le curve
di total, sort e merge sono mostrate insieme.

### Payload distribution

La campagna principale `manySmall50M:50000000:64` copre gia' il caso "grande N, payload piccolo". Per completare la richiesta, si aggiungono due casi con meno record e payload piu' grande, mantenendo una dimensione file dello stesso ordine di grandezza:

```bash
mediumPayload8M:8000000:512
largePayload2M:2000000:2048
```

Thread ridotti:

```bash
1 8 32
```

Questa parte mostra il passaggio da molti record piccoli a meno record con payload piu' grande. Non deve per forza migliorare l'efficiency: payload grandi spesso spostano il collo di bottiglia su I/O e movimento dati.

### MPI strong scaling

Dataset fisso:

```bash
manySmall50M:50000000:64
```

Nodi:

```bash
1 2 4 8
```

Con `RANKS_PER_NODE=1`, i processi MPI sono 1, 2, 4, 8.

Thread per processo:

```bash
1 4 16
```

Il dataset viene generato una volta in `benchmark_data` e poi copiato
automaticamente in `TMP_BASE/mpi_input` sui nodi usati dalla run MPI. La copia
non entra nei tempi del sorter: serve solo a evitare che ogni rank legga la
propria stripe da NFS durante la misura.

Nei summary MPI le colonne fase-per-fase hanno questa semantica:

```text
avg_sort_s  = Fase 1 locale: stripe, sort dei chunk e merge locale del rank
avg_merge_s = Fase 2 distribuita: merge ad albero tra rank MPI
```

Quindi `avg_sort_s` in MPI non e' solo il tempo di `std::sort`, ma l'intera
fase locale prima del merge distribuito.

### MPI weak scaling

Record per nodo:

```bash
weakSmall6250k:6250000:64
```

Quindi:

```text
1 nodo  -> 6.25M record
2 nodi  -> 12.5M record
4 nodi  -> 25M record
8 nodi  -> 50M record
```

## Script principali

- `slurm_tune_single_node.sbatch`: tuning breve di `CHUNK_MB`;
- `slurm_single_node.sbatch`: single-node OpenMP/FastFlow;
- `slurm_mpi_scaling.sbatch`: MPI strong oppure weak;
- `tune_single_node.sh`: loop di tuning;
- `single_node.sh`: esecuzione single-node;
- `mpi_strong.sh`: strong scaling;
- `mpi_weak.sh`: weak scaling;
- `analyze.py`: genera summary CSV e grafici.

## Default attuali

```bash
CHUNK_MB=64
MERGE_FAN=8
PAYLOAD_MAX_BUILD=4096
TRIALS=1
VERIFY=0
RUN_TIMEOUT_SECONDS=180
```

`MERGE_FAN=8` resta nei CSV per compatibilita' e per eventuali run legacy, ma nella versione pipeline/flat viene stampato come non usato. `VERIFY=1` va usato solo su una run piccola finale di correttezza. `RUN_TIMEOUT_SECONDS` vale per le run single-node e serve soprattutto a non far bloccare un job FastFlow.

I file temporanei dei sorter vengono passati con `--tmp-dir` e finiscono sotto
`TMP_BASE`. Anche i dataset generati dai benchmark usano `DATA_DIR`, che di
default e' `$TMP_BASE/spm_benchmark_data`. Gli script Slurm single-node
impostano `DATA_DIR` sotto la directory temporanea del job; lo script MPI tiene
il dataset persistente in `benchmark_data` e lo copia poi in `TMP_BASE/mpi_input`
sui nodi usati. I risultati CSV e i log restano invece in `benchmark_results`.

## Output

Ogni esecuzione crea una cartella dedicata sotto `benchmark_results`:

```text
benchmark_results/
  run_<jobid-o-timestamp>/
    single_node_raw.csv
    single_node_summary.csv
    single_node_tuning_raw.csv
    single_node_tuning_summary.csv
    mpi_strong_raw.csv
    mpi_strong_summary.csv
    mpi_weak_raw.csv
    mpi_weak_summary.csv
    logs/
      *.log
    plots/
      *.png
```

I log sono importanti per commentare:

```text
Fase 1
Fase 2
Totale
```

I grafici in `plots/` mostrano total, phase 1 e phase 2 per single-node, MPI
strong e MPI weak.

## Note metodologiche

- `50M` sostituisce `20M` come caso principale per evitare tempi troppo corti.
- Prima si fa un tuning breve di `CHUNK_MB`, poi si usa la stessa configurazione per la campagna finale.
- OpenMP e FastFlow sono in job separati, cosi' un problema FastFlow non rovina le misure OpenMP.
- Se `RUN_FF=1` ma `ff_sort` non e' stato compilato, lo script fallisce subito invece di saltare FastFlow in silenzio.
- Strong e weak scaling MPI sono separati per mantenere i job leggibili.
- Nelle run MPI lo staging dell'input su `/tmp` locale evita che NFS falsi le curve.
- `node09` non viene usato: gli script Slurm usano `node01-node08`, cioe' i nodi omogenei.

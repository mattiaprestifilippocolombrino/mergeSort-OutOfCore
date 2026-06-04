# Benchmark suite

Questa cartella contiene gli script per misurare speedup, efficiency, strong scaling e weak scaling del progetto.

La struttura e' ispirata a `benchmarks_others`: domini separati, CSV riassuntivi, payload diversi, thread sweep e scaling MPI. La differenza e' che qui i job sono piu' piccoli e usano parametri fissati dopo un tuning breve, per evitare esecuzioni troppo lunghe sullo spmcluster.

### Merge Multi-Pass Semplice (Versione Standard)
La versione standard usa il merge **multi-pass semplice** per OpenMP, FastFlow e MPI. I benchmark single-node passano `--multipass-merge` in modo esplicito, mentre MPI usa il merge locale multi-pass: seriale con `threads_per_rank=1`, parallelo OpenMP con piu' thread. Le vecchie varianti `flat` e pipeline asincrona sono state spostate nelle cartelle `legacy` e non fanno parte dei workflow attivi.

Il multi-pass semplice usa gruppi di massimo `MERGE_FAN` run, produce eventuali intermedi e ripete finche' resta un solo file ordinato. Questa scelta e' piu' prevedibile su cluster HPC: niente writer thread extra, meno rischio di oversubscription e parametri piu' facili da spiegare. Il tuning OpenMP prova combinazioni di `CHUNK_MB` e `MERGE_FAN` su job brevi:

```text
slurm_tune_single_node.sbatch
```

che produce:

```text
benchmark_results/run_<jobid>/single_node_tuning_raw.csv
benchmark_results/run_<jobid>/single_node_tuning_summary.csv
```

Configurazione finale:

```bash
CHUNK_MB_LIST="64"
MERGE_FAN_LIST="8"
THREAD_LIST="1 32"
```

La campagna finale usa `CHUNK_MB=64` e `MERGE_FAN=8`.

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
T_seq              = tempo della versione a 1 thread/worker
T_n                = tempo della versione con n thread/worker
total_speedup      = T_seq_total / T_n_total
total_efficiency   = total_speedup / n
phase1_speedup     = T_seq_fase1 / T_n_fase1
phase1_efficiency  = phase1_speedup / n
phase2_speedup     = T_seq_fase2 / T_n_fase2
phase2_efficiency  = phase2_speedup / n
```

Il summary CSV mantiene anche gli alias storici usati dai grafici:

```text
speedup, efficiency
sort_speedup, sort_efficiency
merge_speedup, merge_efficiency
```

Questo serve a separare lo scaling della Fase 1, cioe' sort dei chunk, dallo
scaling della Fase 2, cioe' merge delle run. Nei grafici single-node le curve
di total, sort e merge sono mostrate insieme.

### Payload distribution

La campagna principale `manySmall50M:50000000:64` copre il caso "grande N, payload piccolo" su single-node. Per completare la richiesta, si aggiungono due casi con meno record e payload piu' grande:

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
manySmall200M:200000000:64
```

Nodi:

```bash
1 2
4 8
```

Con `RANKS_PER_NODE=1`, i processi MPI sono 1, 2, 4, 8.

Thread per processo:

```bash
1 4 8 16 32
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
fase locale prima del merge distribuito. Il merge locale e' seriale solo con
`threads_per_rank=1`; con piu' thread usa il merge OpenMP multi-pass parallelo
e viene marcato come `mpi_local_omp_multipass`.

### MPI weak capacity

La weak non riceve piu' una dimensione statica del dataset. Per ogni coppia
`(nodi, thread/rank)` lo script genera una sonda interna derivata da
`CHUNK_MB`, `MERGE_FAN` e `WEAK_PROBE_CHUNKS_PER_RANK`, misura il tempo del
sorter e normalizza il throughput su `WEAK_TIME_BUDGET_SECONDS=180`.

Il CSV riporta:

```text
avg_capacity_gib_per_node
avg_capacity_total_gib
avg_throughput_gib_node_s
avg_throughput_gib_s
```

Queste colonne rispondono alla domanda: con lo stesso chunk size, merge fan e
numero di thread, quanti GiB vengono processati in 3 minuti per nodo e in
totale.

## Script principali

- `slurm_tune_single_node.sbatch`: tuning breve OpenMP di `CHUNK_MB` e `MERGE_FAN`;
- `slurm_single_node.sbatch`: single-node OpenMP/FastFlow;
- `slurm_mpi_scaling.sbatch`: MPI strong oppure weak;
- `submit_final_mpi_jobs.sh`: sottomette i job MPI finali separati per 1, 2, 4 e 8 nodi;
- `tune_single_node.sh`: loop di tuning;
- `single_node.sh`: esecuzione single-node;
- `mpi_strong.sh`: strong scaling;
- `mpi_weak.sh`: weak scaling;
- `analyze.py`: genera summary CSV e grafici.

La procedura di tuning e' descritta in `GUIDA_TUNING_OPENMP.md`.
`./benchmarks/setup_scratch.sh` non crea `/scratch` dal login node: se lo lanci
fuori da Slurm, sottomette un micro-job di verifica. I benchmark creano comunque
la propria directory scratch all'inizio di ogni job.

## Default attuali

```bash
CHUNK_MB=64
MERGE_FAN=8
PAYLOAD_MAX_BUILD=4096
TRIALS=1
VERIFY=0
RUN_TIMEOUT_SECONDS=180
```

`MERGE_FAN=8` controlla il fan-in massimo del multi-pass nella versione finale. `VERIFY=1` va usato solo su una run piccola finale di correttezza. `RUN_TIMEOUT_SECONDS` vale per le run single-node.
Per MPI, i job finali sono suddivisi per ogni coppia `(nodi, thread/rank)`:
`MPI_STRONG_TIME` vale `00:29:00` di default, mentre `MPI_WEAK_TIME` vale
`00:03:00`. La capacita' weak viene normalizzata su
`WEAK_TIME_BUDGET_SECONDS=180`.

Per i test finali con payload fino a 4096 byte lascia ricompilare gli script,
cioe' non impostare `SKIP_BUILD=1`. Se `SKIP_BUILD=1` viene usato per run
rapide, gli script controllano `BUILD_DIR/CMakeCache.txt` e si fermano se la
build esistente e' stata compilata con un `PAYLOAD_MAX` piu' piccolo di
`PAYLOAD_MAX_BUILD`.

I file temporanei dei sorter vengono passati con `--tmp-dir` e finiscono sotto
`TMP_BASE`, sempre sotto `/scratch` negli script Slurm. Gli script single-node
mettono anche i dataset temporanei sotto `/scratch`; lo script MPI tiene il
dataset sorgente sotto `/scratch` e lo distribuisce poi ai nodi usati in
`TMP_BASE/mpi_input` con `sbcast` quando disponibile. I risultati CSV, summary,
grafici e log restano in `benchmark_results` dentro la cartella del progetto,
quindi la pulizia della workdir scratch a fine job non perde dati.
La variabile `CLEAN_SCRATCH=1` e' attiva di default; usa `CLEAN_SCRATCH=0` solo
per debug.

## Output

Ogni esecuzione crea una cartella dedicata nel progetto:

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

- `50M` e' il dataset fisso per lo strong scaling MPI.
- La weak finale misura capacita' in 3 minuti, non una dimensione dati fissata a priori.
- Prima si fa un tuning breve OpenMP di `CHUNK_MB` e `MERGE_FAN`, poi si usa la stessa configurazione per la campagna finale.
- OpenMP e FastFlow sono in job separati, cosi' un problema FastFlow non rovina le misure OpenMP.
- Se `RUN_FF=1` ma `ff_sort` non e' stato compilato, lo script fallisce subito invece di saltare FastFlow in silenzio.
- Strong e weak scaling MPI sono separati per mantenere i job leggibili.
- Nelle run MPI lo staging dell'input su `/scratch` locale evita che NFS falsi le curve.
- Gli script Slurm non fissano una nodelist: usano i nodi assegnati dal job.

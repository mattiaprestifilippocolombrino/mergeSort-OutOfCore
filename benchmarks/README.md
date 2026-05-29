# Benchmark suite

Questa cartella contiene la campagna di benchmark finale per il progetto.

## Cosa abbiamo preso da `benchmarks_others`

Gli script del collega fanno tre cose utili:

- separano OpenMP, FastFlow e MPI;
- misurano sia payload piccoli sia payload grandi;
- producono CSV e grafici per speedup/efficiency.

Qui manteniamo la stessa idea, ma con meno combinazioni e job separati. L'obiettivo e' rispettare la consegna senza sovraccaricare lo spmcluster.

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

### Payload distribution

Job separata:

```bash
payload16:20000000:16
payload512:2000000:512
fewBig2048:500000:2048
```

Thread ridotti:

```bash
1 8 32
```

Questa parte serve a mostrare cosa cambia passando da molti record piccoli a meno record con payload grande. Non serve aspettarsi efficiency migliore: payload grandi spesso rendono il programma piu' I/O-bound.

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

- `slurm_single_node.sbatch`: single-node OpenMP/FastFlow;
- `slurm_mpi_scaling.sbatch`: MPI strong oppure weak;
- `single_node.sh`: esecuzione single-node;
- `mpi_strong.sh`: strong scaling;
- `mpi_weak.sh`: weak scaling;
- `analyze.py`: genera summary CSV e grafici.

## Parametri consigliati

```bash
CHUNK_MB=128
MERGE_FAN=8
TRIALS=1
VERIFY=0
```

`VERIFY=1` va usato solo su una run piccola finale di correttezza.

## Esecuzione rapida

La sequenza pronta e' in:

```text
benchmarks/GUIDA_MISURAZIONI_CLUSTER.md
```

La guida passo-passo e' in:

```text
benchmarks/GUIDA_CLUSTER_PRINCIPIANTI.md
```

## Output

File principali:

```text
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_raw.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_raw.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/*.log
```

I log sono importanti per commentare:

```text
Fase 1
Fase 2
Totale
```

## Note metodologiche

- `50M` sostituisce `20M` come caso principale per evitare tempi troppo corti.
- `fewBig2048` serve a discutere il comportamento I/O-bound.
- OpenMP e FastFlow sono in job separati, cosi' un errore FastFlow non rovina le misure OpenMP.
- Strong e weak scaling MPI sono separati per mantenere i job leggibili.
- `node09` non viene usato: gli script Slurm usano `node01-node08`, cioe' i nodi omogenei.

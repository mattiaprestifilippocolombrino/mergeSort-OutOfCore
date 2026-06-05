# Analisi: MergeSort Out-of-Core Distribuito (MPI + OpenMP)

---

## 1. Parametri del Modello

| Simbolo | Significato | Valore/Default |
|---------|-------------|----------------|
| **numRecords** | Numero totale di record | — |
| **avgPayload** | Lunghezza media del payload per record | ≤ 4096 B |
| **dimFile** | Dimensione totale del file = numRecords · (12 + avgPayload) B | — |
| **chunkSize** | Dimensione di ogni chunk in RAM | 256 MB (codice), 64 MB (benchmark) |
| **runsPerRank** | Run prodotte per rank = dimFile / (numRank · chunkSize) | — |
| **numRank** | Numero di processi MPI (rank) | — |
| **numThreads** | Thread OpenMP per rank | tutti i core HW |
| **fanMerge** | Fan-in del merge locale multi-pass | 64 (codice), 8 (benchmark) |
| **netCostByte** | Costo per byte trasmesso via rete (= 1 / bandwidth) | — |
| **latencyCost** | Latenza fissa per messaggio MPI | — |
| **tRead** | Tempo per leggere 1 byte da disco | — |
| **tWrite** | Tempo per scrivere 1 byte su disco | — |

> **Formato record su disco:** `[key: 8 B][len: 4 B][payload: len B]`
> L'header è sempre 12 B fissi; la chiave di ordinamento è `key`.

---

## 2. Struttura dell'Algoritmo

L'algoritmo è un **external sort distribuito** con due fasi principali,
modellabili come superstep BSP.

```
━━━ FASE 1 — Sort locale (tutti i rank in parallelo) ━━━━━━━━━━━━━━━━━━━━━━

  [rank 0 solo]  Scansione del file → calcola numRank boundary sicuri su record
                 MPI_Bcast → tutti i rank ricevono i propri offset [start, end)

  [tutti i rank] Ognuno legge la propria stripe di dimFile/numRank byte:
                   • Divide in chunk da chunkSize byte
                   • Sort ogni chunk in RAM con std::sort su RecordIndex (OMP tasks)
                   • Merge locale multi-pass (fan-in fanMerge) → local_sorted.bin

  MPI_Barrier — tutti attendono che la Fase 1 sia completata

━━━ FASE 2 — Merge distribuito ad albero binario ━━━━━━━━━━━━━━━━━━━━━━━━━

  Ripete log₂(numRank) volte con step = 1, 2, 4, …, numRank/2:
    • Sender  → invia local_sorted al proprio receiver   (MPI_Isend + fread sovrapposti)
    • Receiver → riceve il file                          (MPI_Irecv + fwrite sovrapposti)
               → merge 2-way out-of-core con il proprio file

  Dopo log₂(numRank) step: rank 0 possiede l'intero dataset ordinato.
  Rename atomico del file finale nell'output richiesto.
```

**Esempio con numRank = 8:**
```
Step 1:  r1→r0   r3→r2   r5→r4   r7→r6       (numRank/2 coppie, messaggi da dimFile/numRank)
Step 2:  r2→r0            r6→r4               (numRank/4 coppie, messaggi da 2·dimFile/numRank)
Step 4:  r4→r0                                (1 coppia,   messaggio da dimFile/2)
Fine:    rank 0 ha tutto il dataset — dimFile byte ordinati
```

---

## 3. Modello di Costo

### 3.1 Fase 1 — Sort Locale

Ogni rank elabora una stripe di `dimFile/numRank` byte e produce `runsPerRank = dimFile/(numRank·chunkSize)` run ordinate.

**Costo I/O della stripe:**

```
T_lettura  = (dimFile/numRank) · tRead          (lettura della stripe da disco)
T_scrittura = (dimFile/numRank) · tWrite         (scrittura delle run ordinate)
```

**Sort in RAM:** Il sort non muove i payload, ma un vettore di `RecordIndex = {key, offset, len}`
di circa 20 B ciascuno. Il costo di comparazione è trascurabile rispetto all'I/O.

**Merge locale multi-pass:** Con `runsPerRank` run e fan-in `fanMerge`, servono `⌈log_fanMerge(runsPerRank)⌉` passate.
Ogni passata legge e riscrive `dimFile/numRank` byte:

```
T_merge = ⌈log_fanMerge(runsPerRank)⌉ · (dimFile/numRank) · (tRead + tWrite)
```

**Costo totale Fase 1 (per rank):**

```
┌────────────────────────────────────────────────────────────┐
│  T₁ = (dimFile/numRank) · tRead · (1 + ⌈log_fanMerge(runsPerRank)⌉)      │
│       + (dimFile/numRank) · tWrite · ⌈log_fanMerge(runsPerRank)⌉         │
└────────────────────────────────────────────────────────────┘
```

> **Scala come 1/numRank:** raddoppiare numRank dimezza il lavoro per rank — è la fase più efficiente.

**Esempio numerico** (dimFile = 200 GB, numRank = 8, chunkSize = 64 MB, fanMerge = 8):
```
runsPerRank = 200·1024 MB / (8 · 64 MB) = 400 run per rank
⌈log₈(400)⌉ = 3 passate
T₁ ≈ 25 GB · tRead · 4  +  25 GB · tWrite · 3
```

---

### 3.2 Fase 2 — Merge ad Albero Binario

Allo step con distanza `s`, ogni sender invia un file da `D = (dimFile/numRank)·s` byte.

**Costo di comunicazione (pipelining doppio buffer):**

```
T_comm(s) = latencyCost  +  (dimFile/numRank)·s · netCostByte
```

**Costo del merge 2-way del receiver** (fonde due file da `(dimFile/numRank)·s` byte):

```
T_merge(s) = (dimFile/numRank)·s · (2·tRead + 2·tWrite)
```

**Costo totale Fase 2** — somma sui log₂(numRank) step (s = 1, 2, 4, …, numRank/2):

```
  Σ T_comm  = log₂(numRank) · latencyCost  +  dimFile · netCostByte · (1 − 1/numRank)
  Σ T_merge = 2·dimFile · (tRead + tWrite) · (1 − 1/numRank)

┌──────────────────────────────────────────────────────────────────────────────┐
│  T₂ = log₂(numRank)·latencyCost + dimFile·netCostByte·(1−1/numRank) + 2·dimFile·(tRead+tWrite)·(1−1/numRank)│
└──────────────────────────────────────────────────────────────────────────────┘
```

> **Attenzione:** Per numRank grande il termine `2·dimFile·(tRead+tWrite)` converge a `2·dimFile`
> e **non scala con numRank** — rank 0 legge e scrive circa 2·dimFile byte in totale.
> Questo è il **collo di bottiglia principale** della soluzione.

---

### 3.3 Costo Totale

```
T_tot = T₁ + T₂

      = (dimFile/numRank)·tRead·(1 + ⌈log_fanMerge(runsPerRank)⌉)  ← lettura Fase 1
      + (dimFile/numRank)·tWrite·⌈log_fanMerge(runsPerRank)⌉       ← scrittura Fase 1
      + 2·dimFile·(tRead+tWrite)·(1−1/numRank)                     ← I/O merge Fase 2  ← bottleneck
      + dimFile·netCostByte·(1−1/numRank)                          ← comunicazione Fase 2
      + log₂(numRank)·latencyCost                                  ← latenza Fase 2
```

I primi due termini scalano come **1/numRank** (Fase 1 beneficia del parallelismo).
Gli ultimi tre non scalano o crescono con numRank (la Fase 2 domina quando numRank è grande).

---

## 4. Bottleneck

| Fase | Collo di bottiglia | Motivo |
|------|--------------------|--------|
| **Fase 1** | Reader singolo per stripe | Solo un thread legge il file; il sort è parallelizzato ma la lettura no |
| **Fase 1** | Passate di merge locale | ⌈log_fanMerge(runsPerRank)⌉ passate, ognuna legge+scrive dimFile/numRank byte |
| **Fase 2** | I/O del rank 0 | Rank 0 partecipa a tutti i log₂(numRank) step → ~2·dimFile byte di I/O totale |
| **Fase 1** (avvio) | Calcolo boundary seriale | Rank 0 scansiona l'intero file prima del Bcast; gli altri attendono |

**Il bottleneck dominante** è la Fase 2: il suo costo di I/O è indipendente da numRank.
Aggiungere rank riduce il lavoro in Fase 1, ma non riduce i ~2·dimFile di I/O che rank 0
deve svolgere nella Fase 2.

---

## 5. Overlap Computazione–Comunicazione

### 5.1 Pipelining Doppio Buffer (Fase 2)

Sia invio (`mpiSendFile`) che ricezione (`mpiRecvFile`) usano **due buffer alternati**
per sovrapporre I/O su disco e trasmissione di rete.

**Schema del sender (`mpiSendFile`):**
```
                ┌── buffer A ──┐  ┌── buffer B ──┐
Iterazione 1:   │ fread → A   │  │              │
                │ Isend(A) ───┼──┼──────────────┼──→ rete
Iterazione 2:               ↓  │ fread → B   │
                         Wait  │ Isend(B) ───┼──→ rete
                               │ swap A↔B    │
```

Mentre il blocco `i` viaggia in rete via `MPI_Isend`, il disco legge già il blocco `i+1`.
Simmetricamente nel receiver: mentre `MPI_Irecv` riceve il blocco `i+1` in rete,
il disco scrive il blocco `i`.

**Guadagno:**
```
Senza pipelining:  T = D · tRead + D · netCostByte   (disco poi rete, in sequenza)
Con pipelining:    T ≈ D · max(tRead, netCostByte)   (disco e rete in parallelo)
```
Il beneficio è massimo quando banda disco ≈ banda rete.

### 5.2 Overlap Lettura–Sort (Fase 1)

Il thread principale legge chunk sequenzialmente e lancia task OpenMP per ordinarli.
Gli altri `numThreads−1` thread eseguono il sort in parallelo alla lettura:

```
Thread 0 (reader):   ─[legge C₀]─[legge C₁]─[legge C₂]──→
Thread 1…numThreads (task):─[sort C₀]──[sort C₁]──[sort C₂]──→
```

La finestra `taskWindow = max(2, 2·numThreads)` limita il numero di chunk in volo
per non saturare la RAM. Se `taskWindow` viene raggiunto, il reader aspetta
il completamento di tutti i task prima di leggere altri chunk.

---

## 6. Sfide Incontrate e Soluzioni

**6.1 Boundary sicuri sui record**
Il file non ha magic number: un taglio matematico `dimFile/numRank · rank` potrebbe spezzare un record a metà.
→ `computeRecordBoundaries` scorre record per record (legge header, salta payload con `fseeko`)
e garantisce che ogni boundary cada all'inizio di un record. Il vettore è poi diffuso con `MPI_Bcast`.

**6.2 Overflow MPI per file grandi**
MPI usa `int` per il count: un file da decine di GB supererebbe `INT_MAX ≈ 2.1 GB`.
→ Il file è suddiviso in blocchi da `PIPE_CHUNK = 64 MB`, ognuno inviato separatamente.
La dimensione totale è comunicata come `int64_t` in un messaggio preliminare (tag `tagSize`).

**6.3 Incroci di messaggi tra step diversi**
Su reti asincrone, messaggi di step diversi potrebbero essere abbinati tra loro.
→ Ogni step usa tag unici: `tagSize = 100 + step`, `tagData = 200 + step`.

**6.4 numRank non potenza di 2**
Ad alcuni step non esiste un sender per tutti i receiver.
→ Il receiver controlla `if (sender < numRank)` e salta il passo se il sender non esiste.

**6.5 Thread safety MPI**
OpenMP usa più thread, ma solo il thread principale chiama funzioni MPI.
→ `MPI_Init_thread` con livello `MPI_THREAD_FUNNELED`: il minimo necessario, massima portabilità.

---

## 7. Ottimizzazioni

| Ottimizzazione | Fase | Effetto |
|----------------|------|---------|
| Pipelining doppio buffer (Isend/Irecv) | Fase 2 | Sovrappone I/O disco e trasmissione rete |
| Merge ad albero binario | Fase 2 | Distribuisce il merge; evita la saturazione del master |
| Tag MPI univoci per step | Fase 2 | Previene incroci su reti asincrone |
| Buffer 8 MB in mpiRecvFile (setvbuf) | Fase 2 | Riduce syscall di scrittura durante la ricezione |
| Sort su RecordIndex (~20 B) | Fase 1 | Sposta indici leggeri, non payload pesanti |
| OMP tasks + taskWindow | Fase 1 | Sovrappone lettura disco e sort in RAM; evita OOM |
| OMP tasks per gruppi di merge (parallelMerge) | Fase 1 | Merge locale parallelo con numThreads > 1 thread |
| Buffer 4 MB nella scansione boundary | Fase 1 | Riduce syscall; sfrutta il read-ahead del kernel |
| Directory tmp esclusiva per rank (spm_mpi_rN) | Tutte | Evita collisioni I/O tra rank sullo stesso nodo |
| RAII TempDir | Tutte | Pulizia automatica dei file temporanei anche in caso di eccezione |
| fread_unlocked | Fase 1 | Elimina l'overhead del lock della libc nelle letture single-thread |
| MPI_THREAD_FUNNELED | Init | Minima interferenza tra OMP e MPI; massima portabilità |

---

## 8. Scalabilità

### Strong Scaling — dimFile fisso, numRank cresce

| Fase | Scaling | Limite |
|------|---------|--------|
| Fase 1 — sort locale | **∝ 1/numRank** (quasi ideale) | Reader singolo; ⌈log_fanMerge(runsPerRank)⌉ passate di merge |
| Fase 2 — I/O merge | **Costante** (~2·dimFile, non scala) | Rank 0 fa ~2·dimFile di I/O indipendentemente da numRank |
| Fase 2 — latenza | Cresce come **log₂(numRank)** | Un messaggio per step |
| Boundary (in Fase 1) | **Costante** rispetto a numRank | Seriale su rank 0, cresce solo con dimFile |

Fino a numRank moderato, la riduzione del lavoro in Fase 1 domina e lo speedup cresce.
Oltre una soglia, il termine fisso `2·dimFile·(tRead+tWrite)` della Fase 2 satura lo speedup.

### Weak Scaling — dimFile/numRank fisso, numRank cresce

| Fase | Scaling | Limite |
|------|---------|--------|
| Fase 1 | **Costante** (ideale) | Ogni rank processa sempre dimFile/numRank byte |
| Fase 2 — I/O | Cresce linearmente con dimFile = numRank·(dimFile/numRank) | Il dataset totale cresce con numRank |
| Fase 2 — latenza | Cresce come **log₂(numRank)** | — |
| Boundary | Cresce **linearmente** con dimFile | Rank 0 scansiona un file sempre più grande |

L'ostacolo principale al weak scaling è il calcolo dei boundary:
se numRank raddoppia e dimFile raddoppia, rank 0 deve scansionare il doppio dei byte
in modo sequenziale, allungando il tempo di avvio della Fase 1 per tutti i rank.

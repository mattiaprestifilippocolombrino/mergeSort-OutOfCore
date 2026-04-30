#pragma once

/*
Modulo che definisce il formato dei record letti dal file di input, e le funzioni per leggere
gli header dal file di input, saltare il payload, e scrivere i record nel file di output.
Viene usato da entrambe le versioni, sia omp, sia fastflow.
Il layout dei dati è il seguente: [key (8 byte)][len (4 byte)][payload (len byte)].
La dimensione di ogni record è quindi 12 + len byte.
*/

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>


// Costanti


// Dimensione massima del payload. Modificabile a compile-time con -DPAYLOAD_MAX=N.
#ifndef PAYLOAD_MAX
static constexpr uint32_t PAYLOAD_MAX = 4096;
#endif

// Sanity check: PAYLOAD_MAX deve essere almeno 8.
static_assert(PAYLOAD_MAX >= 8, "PAYLOAD_MAX deve essere >= 8");

// Dimensione dell'header su disco (key + len): 8B + 4B = 12B.
static constexpr uint32_t HEADER_SIZE = sizeof(uint64_t) + sizeof(uint32_t);


// Struttura che modella l'header di un record.
struct RecordHeader {
    uint64_t key;  // chiave di ordinamento
    uint32_t len;  // lunghezza del payload in byte

    // Dimensione totale del record su disco (header + payload).
    uint32_t total_size() const {
        return HEADER_SIZE + len;
    }
};


/*
Funzione che serve a leggere l’header di un record binario da un file e salvarlo nella struttura hdr.
L’header contiene due campi: key → 8 byte (uint64_t) e len → 4 byte (uint32_t)
Quindi ogni record nel file inizia con: [ key (8 byte) ][ len (4 byte) ][ payload ... ]
Si ha in input un file già aperto da cui leggere e una struttura dove salvare i dati letti.
Viene restituitp in output true se l'header viene letto correttamente, false se non viene letto nessun dato.
*/

inline bool read_header(FILE* f, RecordHeader& hdr) {
    // Viene utilizzato un buffer locale di 12 byte per leggere key (8B) + len (4B) in una sola operazione.
    uint8_t buf[HEADER_SIZE];

    /*Si legge 12 byte dal file e si mettono in buf.
    Si usa fread_unlocked per evitare l'overhead del mutex lock/unlock della libc
    ad ogni singola chiamata, poichè usiamo un solo thread. 
    */
    size_t bytes_read = fread_unlocked(buf, 1, HEADER_SIZE, f);

    // Se non viene letto niente, siamo alla fine del file, e quindi ritorniamo false.
    if (bytes_read == 0) {
        return false;
    }

    // Se viene letto meno di 12 byte, il file è troncato.
    if (bytes_read != HEADER_SIZE) {
        throw std::runtime_error("read_header: file troncato");
    }

    // Prende i dati grezzi dal buffer e li copia nella struct. Copia i primi 8 byte in key e i successivi 4 in len.
    std::memcpy(&hdr.key, buf,     sizeof(uint64_t));
    std::memcpy(&hdr.len, buf + 8, sizeof(uint32_t));

    // Sanity check sul len: deve essere nel range [8, PAYLOAD_MAX].
    if (hdr.len < 8 || hdr.len > PAYLOAD_MAX) {
        throw std::runtime_error(
            "read_header: len=" + std::to_string(hdr.len) + " fuori da [8, PAYLOAD_MAX]"
        );
    }

    return true;
}


// Funzione usata per saltare il payload di un record nel file, senza leggerlo in memoria.
// Si sposta il cursore del file in avanti di len byte.
inline void skip_payload(FILE* f, uint32_t len) {
    // Converto esplicitamente len a off_t (tipo usato da fseeko per offset a 64 bit).
    off_t offset = static_cast<off_t>(len);

    // fseeko sposta il cursore del file f di offset byte dalla posizione corrente.
    if (fseeko(f, offset, SEEK_CUR) != 0) {
        throw std::runtime_error("skip_payload: fseek fallito");
    }
}


// Funzione che scrive un record completo (header + payload) su FILE*.
// Prende in input il file, la key, la lunghezza del payload e il payload stesso.
inline void write_record(FILE* f, uint64_t key, uint32_t len, const char* payload) {
    // Si ricostruisce l'header in un buffer locale di 12 byte: buf[0..7]  = key (8 byte). buf[8..11] = len (4 byte)
    uint8_t buf[HEADER_SIZE];
    std::memcpy(buf,     &key, sizeof(uint64_t));
    std::memcpy(buf + 8, &len, sizeof(uint32_t));
    // Si scrive nel file in un colpo sola prima l'header e poi il payload.
    // Uso fwrite_unlocked invece di std::fwrite. Essendo chiamate
    // milioni di volte durante il merge, l'assenza del lock sul file velocizza
    // drasticamente i tempi di scrittura riducendo i context-switch inutili.
    size_t written_header  = fwrite_unlocked(buf,     1, HEADER_SIZE, f);
    size_t written_payload = fwrite_unlocked(payload, 1, len,          f);

    // Se la scrittura del record non è andata a buon fine, viene lanciata un'eccezione.
    if (written_header != HEADER_SIZE || written_payload != len) {
        throw std::runtime_error("write_record: fwrite fallita");
    }
}

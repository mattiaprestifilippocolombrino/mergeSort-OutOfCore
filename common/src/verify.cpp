// =============================================================================
// verify.cpp  -  verifica ordinamento e, opzionalmente, equivalenza input/output
// =============================================================================
//
// Questo tool serve nei test automatici e nei benchmark.
//
// Con un solo argomento:
//   controlla che l'output sia ordinato per key.
//
// Con due argomenti:
//   controlla anche che input e output contengano gli stessi record.
//
// Nota importante:
// per non caricare tutto il file in RAM, non confronto i record uno per uno
// con una mappa. Scansiono invece i file in streaming e calcolo due impronte
// aggregate (somma e xor degli hash dei record), insieme a count e byte totali.
// Per un verificatore pratico da benchmark e' veloce e out-of-core; una
// collisione teorica e' possibile, ma estremamente improbabile.
//
// Utilizzo:
//   ./verify <output>
//   ./verify <input> <output>
//
// Con un solo file controlla che le chiavi siano non decrescenti.
// Con due file controlla anche che input e output contengano gli stessi record.
// =============================================================================

#include "record.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

struct FileStats {
    // Numero di record letti.
    uint64_t count = 0;
    // Numero totale di byte logici letti: HEADER_SIZE + len per ogni record.
    uint64_t bytes = 0;
    // Range delle chiavi, utile sia per debug sia per report.
    uint64_t minKey = std::numeric_limits<uint64_t>::max();
    uint64_t maxKey = 0;
    // Due aggregati indipendenti sugli hash dei record.
    // La somma e' sensibile alla molteplicita', lo xor ai bit cambiati.
    uint64_t hashSum = 0;
    uint64_t hashXor = 0;
    // Numero di violazioni dell'ordinamento non decrescente.
    uint64_t orderErrors = 0;
};

static void usage(const char* p) {
    std::cerr << "Utilizzo: " << p << " <output> oppure " << p << " <input> <output>\n";
    std::exit(1);
}

static uint64_t fnv1aUpdate(uint64_t h, const void* data, size_t n) {
    // FNV-1a a 64 bit: semplice, veloce e sufficiente per un verifier.
    // Aggiorno l'hash byte per byte per includere header e payload.
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

static FileStats scanFile(const std::string& path, bool checkOrder) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        throw std::runtime_error("Impossibile aprire: " + path);
    std::setvbuf(f, nullptr, _IOFBF, 4 * 1024 * 1024);

    FileStats s;
    uint64_t prevKey = 0;
    // Il payload puo' essere grande; lo leggo a blocchi per tenere memoria
    // costante anche su file molto grandi.
    std::array<char, 64 * 1024> payloadBuf{};

    RecordHeader hdr;
    while (readHeader(f, hdr)) {
        // Hash del record completo: key, len e payload.
        // In questo modo il verifier controlla anche che il payload non sia
        // stato perso o associato alla key sbagliata.
        uint64_t recHash = 1469598103934665603ULL;
        recHash = fnv1aUpdate(recHash, &hdr.key, sizeof(hdr.key));
        recHash = fnv1aUpdate(recHash, &hdr.len, sizeof(hdr.len));

        uint32_t remaining = hdr.len;
        while (remaining > 0) {
            // Streaming del payload: nessuna assunzione che tutto stia in RAM.
            size_t batch = std::min<size_t>(remaining, payloadBuf.size());
            if (std::fread(payloadBuf.data(), 1, batch, f) != batch)
                throw std::runtime_error("Payload troncato in " + path);
            recHash = fnv1aUpdate(recHash, payloadBuf.data(), batch);
            remaining -= static_cast<uint32_t>(batch);
        }

        if (checkOrder && s.count > 0 && hdr.key < prevKey) {
            // Non stampo tutte le violazioni: con un file molto sbagliato
            // potrei produrre milioni di righe inutili.
            if (s.orderErrors < 10) {
                std::cerr << "[ERR] record " << s.count
                          << ": key=" << hdr.key << " < prev=" << prevKey << "\n";
            }
            ++s.orderErrors;
        }

        prevKey = hdr.key;
        // Aggiornamento statistiche globali.
        s.minKey = std::min(s.minKey, hdr.key);
        s.maxKey = std::max(s.maxKey, hdr.key);
        s.count++;
        s.bytes += HEADER_SIZE + hdr.len;
        s.hashSum += recHash;
        s.hashXor ^= recHash;
    }

    if (std::fclose(f) != 0)
        throw std::runtime_error("Errore chiusura file: " + path);

    if (s.count == 0)
        // Per file vuoto evito di lasciare min_key al valore massimo uint64_t.
        s.minKey = 0;
    return s;
}

static void printStats(const char* label, const FileStats& s) {
    std::cout << label << "\n"
              << "  Record : " << s.count << "\n"
              << "  Byte   : " << s.bytes << "\n"
              << "  Key min: " << s.minKey << "\n"
              << "  Key max: " << s.maxKey << "\n";
}

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 3)
        usage(argv[0]);

    try {
        if (argc == 2) {
            // Modalita' semplice: controllo solo che l'output sia ordinato.
            FileStats out = scanFile(argv[1], true);
            printStats("--- Verifica output ---", out);
            std::cout << "  Errori ordine: " << out.orderErrors << "\n"
                      << "  Stato        : " << (out.orderErrors == 0 ? "OK" : "FALLITO") << "\n";
            return out.orderErrors == 0 ? 0 : 1;
        }

        // Modalita' completa: input e output devono avere la stessa impronta
        // aggregata e l'output deve essere ordinato.
        FileStats in = scanFile(argv[1], false);
        FileStats out = scanFile(argv[2], true);

        bool sameRecords =
            in.count == out.count &&
            in.bytes == out.bytes &&
            in.hashSum == out.hashSum &&
            in.hashXor == out.hashXor;

        printStats("--- Input ---", in);
        printStats("--- Output ---", out);
        std::cout << "  Errori ordine: " << out.orderErrors << "\n"
                  << "  Stessi record: " << (sameRecords ? "si" : "no") << "\n"
                  << "  Stato        : "
                  << ((sameRecords && out.orderErrors == 0) ? "OK" : "FALLITO") << "\n";

        return (sameRecords && out.orderErrors == 0) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "[ERR] " << e.what() << "\n";
        return 1;
    }
}

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
    uint64_t min_key = std::numeric_limits<uint64_t>::max();
    uint64_t max_key = 0;
    // Due aggregati indipendenti sugli hash dei record.
    // La somma e' sensibile alla molteplicita', lo xor ai bit cambiati.
    uint64_t hash_sum = 0;
    uint64_t hash_xor = 0;
    // Numero di violazioni dell'ordinamento non decrescente.
    uint64_t order_errors = 0;
};

static void usage(const char* p) {
    std::cerr << "Utilizzo: " << p << " <output> oppure " << p << " <input> <output>\n";
    std::exit(1);
}

static uint64_t fnv1a_update(uint64_t h, const void* data, size_t n) {
    // FNV-1a a 64 bit: semplice, veloce e sufficiente per un verifier.
    // Aggiorno l'hash byte per byte per includere header e payload.
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

static FileStats scan_file(const std::string& path, bool check_order) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        throw std::runtime_error("Impossibile aprire: " + path);
    std::setvbuf(f, nullptr, _IOFBF, 4 * 1024 * 1024);

    FileStats s;
    uint64_t prev_key = 0;
    // Il payload puo' essere grande; lo leggo a blocchi per tenere memoria
    // costante anche su file molto grandi.
    std::array<char, 64 * 1024> payload_buf{};

    RecordHeader hdr;
    while (read_header(f, hdr)) {
        // Hash del record completo: key, len e payload.
        // In questo modo il verifier controlla anche che il payload non sia
        // stato perso o associato alla key sbagliata.
        uint64_t rec_hash = 1469598103934665603ULL;
        rec_hash = fnv1a_update(rec_hash, &hdr.key, sizeof(hdr.key));
        rec_hash = fnv1a_update(rec_hash, &hdr.len, sizeof(hdr.len));

        uint32_t remaining = hdr.len;
        while (remaining > 0) {
            // Streaming del payload: nessuna assunzione che tutto stia in RAM.
            size_t batch = std::min<size_t>(remaining, payload_buf.size());
            if (std::fread(payload_buf.data(), 1, batch, f) != batch)
                throw std::runtime_error("Payload troncato in " + path);
            rec_hash = fnv1a_update(rec_hash, payload_buf.data(), batch);
            remaining -= static_cast<uint32_t>(batch);
        }

        if (check_order && s.count > 0 && hdr.key < prev_key) {
            // Non stampo tutte le violazioni: con un file molto sbagliato
            // potrei produrre milioni di righe inutili.
            if (s.order_errors < 10) {
                std::cerr << "[ERR] record " << s.count
                          << ": key=" << hdr.key << " < prev=" << prev_key << "\n";
            }
            ++s.order_errors;
        }

        prev_key = hdr.key;
        // Aggiornamento statistiche globali.
        s.min_key = std::min(s.min_key, hdr.key);
        s.max_key = std::max(s.max_key, hdr.key);
        s.count++;
        s.bytes += HEADER_SIZE + hdr.len;
        s.hash_sum += rec_hash;
        s.hash_xor ^= rec_hash;
    }

    if (std::fclose(f) != 0)
        throw std::runtime_error("Errore chiusura file: " + path);

    if (s.count == 0)
        // Per file vuoto evito di lasciare min_key al valore massimo uint64_t.
        s.min_key = 0;
    return s;
}

static void print_stats(const char* label, const FileStats& s) {
    std::cout << label << "\n"
              << "  Record : " << s.count << "\n"
              << "  Byte   : " << s.bytes << "\n"
              << "  Key min: " << s.min_key << "\n"
              << "  Key max: " << s.max_key << "\n";
}

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 3)
        usage(argv[0]);

    try {
        if (argc == 2) {
            // Modalita' semplice: controllo solo che l'output sia ordinato.
            FileStats out = scan_file(argv[1], true);
            print_stats("--- Verifica output ---", out);
            std::cout << "  Errori ordine: " << out.order_errors << "\n"
                      << "  Stato        : " << (out.order_errors == 0 ? "OK" : "FALLITO") << "\n";
            return out.order_errors == 0 ? 0 : 1;
        }

        // Modalita' completa: input e output devono avere la stessa impronta
        // aggregata e l'output deve essere ordinato.
        FileStats in = scan_file(argv[1], false);
        FileStats out = scan_file(argv[2], true);

        bool same_records =
            in.count == out.count &&
            in.bytes == out.bytes &&
            in.hash_sum == out.hash_sum &&
            in.hash_xor == out.hash_xor;

        print_stats("--- Input ---", in);
        print_stats("--- Output ---", out);
        std::cout << "  Errori ordine: " << out.order_errors << "\n"
                  << "  Stessi record: " << (same_records ? "si" : "no") << "\n"
                  << "  Stato        : "
                  << ((same_records && out.order_errors == 0) ? "OK" : "FALLITO") << "\n";

        return (same_records && out.order_errors == 0) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "[ERR] " << e.what() << "\n";
        return 1;
    }
}

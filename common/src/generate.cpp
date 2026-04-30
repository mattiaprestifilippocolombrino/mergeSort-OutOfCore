// =============================================================================
// generate.cpp  —  generatore di file di record per i test
// =============================================================================
//
// Questo programma non fa parte dell'algoritmo di sorting vero e proprio:
// serve per creare input riproducibili con record a lunghezza variabile.
//
// E' utile per il report di prestazioni perche' permette di cambiare:
//   - numero di record;
//   - payload minimo/massimo;
//   - distribuzione delle chiavi: random, gia' ordinate, reverse.
//
// Il formato scritto e' esattamente quello usato dal sorter:
//
//   [key: 8 byte][len: 4 byte][payload: len byte]
//
// Il payload e' casuale e non viene mai interpretato dal MergeSort.
//
// Utilizzo:
//   ./generate <output> <N> [--payload-max B] [--payload-min B]
//              [--seed S] [--sorted] [--reverse]
// =============================================================================

#include "record.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

static void usage(const char* prog) {
    std::cerr << "Utilizzo: " << prog << " <output> <N> [opzioni]\n"
              << "  --payload-max B   Max payload in byte (default: PAYLOAD_MAX=" << PAYLOAD_MAX << ")\n"
              << "  --payload-min B   Min payload in byte (default: 8)\n"
              << "  --seed        S   Seed RNG (default: 42)\n"
              << "  --sorted          Chiavi in ordine crescente\n"
              << "  --reverse         Chiavi in ordine decrescente\n";
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) usage(argv[0]);

    // Parametri principali.
    // I valori di default sono scelti per avere un test realistico ma semplice.
    std::string outputPath = argv[1];
    uint64_t    N           = std::stoull(argv[2]);
    uint32_t    pmax        = PAYLOAD_MAX;
    uint32_t    pmin        = 8;
    uint64_t    seed        = 42;
    bool        sorted      = false;
    bool        reverse     = false;

    // Parsing volutamente lineare: per un progetto didattico e' piu' leggibile
    // di una libreria di command line parsing, e qui le opzioni sono poche.
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "--payload-max" && i+1 < argc) pmax    = std::stoul(argv[++i]);
        else if (a == "--payload-min" && i+1 < argc) pmin    = std::stoul(argv[++i]);
        else if (a == "--seed"        && i+1 < argc) seed    = std::stoull(argv[++i]);
        else if (a == "--sorted")                    sorted  = true;
        else if (a == "--reverse")                   reverse = true;
        else usage(argv[0]);
    }

    // Controlli sul payload:
    // il sorter assume sempre 8 <= len <= PAYLOAD_MAX, quindi anche il
    // generatore deve rispettare lo stesso contratto.
    if (pmax > PAYLOAD_MAX)
        throw std::runtime_error("payload-max > PAYLOAD_MAX=" + std::to_string(PAYLOAD_MAX));
    if (pmin < 8 || pmin > pmax)
        throw std::runtime_error("payload-min fuori range [8, payload-max]");

    FILE* fout = std::fopen(outputPath.c_str(), "wb");
    if (!fout) throw std::runtime_error("Impossibile creare: " + outputPath);
    // Buffer da 8 MB per scritture veloci.
    // Le fwrite di write_record restano semplici, ma la libc accumula i dati
    // e riduce il numero di chiamate al sistema operativo.
    std::setvbuf(fout, nullptr, _IOFBF, 8 * 1024 * 1024);

    // Usa mt19937_64 per velocità; il payload viene generato con XOR-shift
    // sul seed per evitare N chiamate a uniform_distribution per ogni byte.
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> keyRnd;
    std::uniform_int_distribution<uint32_t> lenRnd(pmin, pmax);

    // Buffer di payload pre-allocato e garantito allineato a 8 byte.
    // Riempiamo con 64-bit alla volta (8× più veloce dei singoli byte).
    // Poi passiamo a write_record solo i primi len byte.
    std::vector<uint64_t> payloadBuf((pmax + 7) / 8);
    char* payload = reinterpret_cast<char*>(payloadBuf.data());

    std::cout << "Genero " << N << " record → " << outputPath << "\n"
              << "  payload_min=" << pmin << "B  payload_max=" << pmax << "B\n";

    for (uint64_t i = 0; i < N; i++) {
        uint64_t key;
        // Tre casi utili nei benchmark:
        //   random  -> caso generale;
        //   sorted  -> input gia' ordinato;
        //   reverse -> input pessimo per molti algoritmi semplici.
        // std::sort gestisce bene tutti e tre, ma misurarli e' utile.
        if      (sorted)  key = i;
        else if (reverse) key = N - 1 - i;
        else              key = keyRnd(rng);

        uint32_t len = lenRnd(rng);

        // Riempie il payload 8 byte alla volta (loop vectorizzabile).
        uint32_t n64 = len / 8;
        for (uint32_t b = 0; b < n64; b++)
            payloadBuf[b] = rng();
        // Riempie i byte rimanenti (0..7).
        uint64_t tail = rng();
        std::memcpy(payload + n64 * 8, &tail, len % 8);

        // Scrittura nel formato comune del progetto.
        writeRecord(fout, key, len, payload);

        // Progress minimale: comodo per file grandi, ma senza stampare a ogni
        // record per non rallentare la generazione.
        if ((i + 1) % 1'000'000 == 0)
            std::cout << "  " << (i+1)/1'000'000 << "M record scritti\r" << std::flush;
    }
    if (N >= 1'000'000) std::cout << "\n";

    std::fclose(fout);
    std::cout << "File generato: " << outputPath << " (" << N << " record)\n";
    return 0;
}

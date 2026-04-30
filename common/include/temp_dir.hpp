#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>

/*
Modulo che gestisce la directory temporanea dove vengono salvati
i file intermedi. Viene usato da entrambe le versioni, sia omp, sia fastflow.
La directory viene creata all'avvio del programma e viene eliminata alla fine
del programma, a meno che non venga specificato l'argomento -k o --keep.
I file intermedi hanno nomi semplici, ad esempio: run_0.bin, run_1.bin, run_p1_0.bin.
Se due esecuzioni usassero la stessa tmp-dir, potrebbero sovrascriversi.
Creo quindi una sottodirectory diversa per ogni processo. A fine esecuzione
la cancello automaticamente, a meno che l'utente non abbia chiesto keep=true.
*/
class TempDir {
    std::filesystem::path path_;    //Percorso della directory temporanea.
    bool                  keep_;    //Se true, la directory non viene cancellata alla fine.

public:
    TempDir(const std::string& base_dir,
            const std::string& prefix,
            bool keep = false) // Costruttore: base_dir è la directory padre (es: /tmp/spm_bench),
                                 // prefix è un prefisso per il nome della sottodirectory,
                                 // keep è un flag per mantenere o meno la directory alla fine.
    {
        keep_ = keep;

        // Creo la directory base se non esiste, ad esempio /tmp/spm_bench.
        std::filesystem::create_directories(base_dir);

        // Uso pid + timestamp + tentativo. 
        // std::chrono::steady_clock::now() restituisce un time_point.
        std::chrono::steady_clock::time_point  now_tp   = std::chrono::steady_clock::now();
        // .time_since_epoch() lo converte in una durata dall'epoch.
        std::chrono::steady_clock::duration    duration  = now_tp.time_since_epoch();
        // .count() la converte in un intero (in nanosec o tic a seconda della piattaforma).
        std::chrono::steady_clock::rep         now_count = duration.count();

        // .getpid() restituisce il pid del processo.
        pid_t pid = ::getpid();

        for (int attempt = 0; attempt < 100; ++attempt) {
            // Compongo il nome: prefix_PID_TIMESTAMP_TENTATIVO
            std::string dir_name = prefix
                + "_" + std::to_string(pid)
                + "_" + std::to_string(now_count)
                + "_" + std::to_string(attempt);

            // Creo il percorso della directory temporanea usando il percorso base e il nome della directory.
            path_ = std::filesystem::path(base_dir) / dir_name;

            std::error_code ec;
            // Creo la directory temporanea. create_directory fallisce se il nome esiste gia'.
            // In quel caso provo con attempt successivo.
            bool created = std::filesystem::create_directory(path_, ec);
            if (created) {
                return; // successo: la directory e' stata creata
            }
        }

        throw std::runtime_error(
            "TempDir: impossibile creare directory temporanea in " + base_dir
        );
    }

    // Vieto costruttore di copia e operatore di assegnazione.
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;

    ~TempDir() {
        // Il distruttore viene chiamato automaticamente quando l’oggetto esce dallo scope.
        // Elimino la directory temporanea se keep_ è false e path_ non è vuoto.
        bool should_delete = !keep_ && !path_.empty();
        if (should_delete) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }

    std::string str() const {
        return path_.string();
    }
};

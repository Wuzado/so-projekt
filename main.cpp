#include <iostream>
#include <optional>
#include "common.h"
#include "dyrektor/dyrektor.h"
#include "logger.h"
#include "petent/generator.h"
#include "petent/petent.h"
#include "rejestracja/rejestracja.h"
#include "urzednik/urzednik.h"

void print_usage(char* program_name) {
    std::cerr << "Uzycie: " << program_name << " <argumenty>\n\n"
              << "Ogolne argumenty:\n"
              << "  --role <rola>  "
              << "Okresla role (dyrektor/petent/rejestracja/urzednik/generator)\n"
              << "Argumenty dyrektora:\n"
              << "  --Tp <godzina>  "
              << "Godzina otwarcia urzedu (0-23), domyslnie 8\n"
              << "  --Tk <godzina>  "
              << "Godzina zamkniecia urzedu (0-23), domyslnie 16\n"
              << "Argumenty urzednika:\n"
              << "  --dept <SC|KM|ML|PD|SA>  "
              << "Wydzial urzednika\n";
}

struct Config {
    Identity role = Identity::Dyrektor;
    int Tp = 8;
    int Tk = 16;
    std::optional<UrzednikRole> urzednik_role;

    static std::optional<Config> parse_arguments(int argc, char* argv[]) {
        Config config;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (arg == "--role" && i + 1 < argc) {
                auto identity_opt = string_to_identity(argv[++i]);
                if (!identity_opt) {
                    std::cerr << "Blad: Nieznana rola: " << argv[i] << "\n";
                    return std::nullopt;
                }
                config.role = *identity_opt;
            }
            else if (arg == "--Tp" && i + 1 < argc) {
                config.Tp = std::stoi(argv[++i]);
                if (config.Tp < 0 || config.Tp > 23) {
                    std::cerr << "Blad: --Tp musi byc w zakresie 0-23\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--Tk" && i + 1 < argc) {
                config.Tk = std::stoi(argv[++i]);
                if (config.Tk < 0 || config.Tk > 23) {
                    std::cerr << "Blad: --Tk musi byc w zakresie 0-23\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--dept" && i + 1 < argc) {
                auto role_opt = string_to_urzednik_role(argv[++i]);
                if (!role_opt) {
                    std::cerr << "Blad: Nieznany wydzial urzednika: " << argv[i] << "\n";
                    return std::nullopt;
                }
                config.urzednik_role = *role_opt;
            }
            else {
                std::cerr << "Blad: Nieznany argument: " << arg << "\n";
                return std::nullopt;
            }
        }

        // Validate Tp < Tk
        if (config.Tp >= config.Tk) {
            std::cerr << "Blad: Godzina otwarcia (--Tp) musi byc wczesniejsza niz godzina zamkniecia (--Tk)\n";
            return std::nullopt;
        }

        return config;
    }
};

int main(int argc, char* argv[]) {
    Logger::clear_log();

    auto config = Config::parse_arguments(argc, argv);
    if (!config) {
        print_usage(argv[0]);
        return 1;
    }

    Logger::log(LogSeverity::Debug, config->role, "Config:" 
        " Tp=" + std::to_string(config->Tp) +
        " Tk=" + std::to_string(config->Tk)
    );

    switch (config->role) {
        case Identity::Dyrektor:
            dyrektor_main({config->Tp, config->Tk});
            break;
        case Identity::Rejestracja:
            rejestracja_main();
            break;
        case Identity::Urzednik:
            if (!config->urzednik_role) {
                Logger::log(LogSeverity::Err, Identity::Urzednik, "Brak parametru --dept.");
                return 1;
            }
            urzednik_main(*config->urzednik_role);
            break;
        case Identity::Petent:
            petent_main();
            break;
        case Identity::Generator:
            generator_main();
            break;
    }

    Logger::log(LogSeverity::Debug, config->role, "Koniec dzialania procesu.");
    return 0;
}

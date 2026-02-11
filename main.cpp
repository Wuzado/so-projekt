#include <array>
#include <iostream>
#include <optional>
#include "common.h"
#include "dyrektor/dyrektor.h"
#include "kasa/kasa.h"
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
              << "  --time-mul <mnoznik>  "
              << "Mnoznik czasu symulacji, domyslnie 1000\n"
              << "Argumenty dyrektora:\n"
              << "  --Tp <godzina>  "
              << "Godzina otwarcia urzedu (0-23), domyslnie 8\n"
              << "  --Tk <godzina>  "
              << "Godzina zamkniecia urzedu (0-23), domyslnie 16\n"
              << "  --N <liczba>  "
              << "Maksymalna liczba petentow w budynku, domyslnie 100\n"
              << "  --X1 <limit>    "
              << "Limit przyjec dla urzednikow SA (na urzednika), domyslnie 2000\n"
              << "  --X2 <limit>    "
              << "Limit przyjec dla urzednika SC, domyslnie 1000\n"
              << "  --X3 <limit>    "
              << "Limit przyjec dla urzednika KM, domyslnie 1000\n"
              << "  --X4 <limit>    "
              << "Limit przyjec dla urzednika ML, domyslnie 1000\n"
              << "  --X5 <limit>    "
              << "Limit przyjec dla urzednika PD, domyslnie 1000\n"
              << "  --gen-from-dyrektor  "
              << "Uruchamia generator petentow jako proces potomny dyrektora\n"
              << "  --one-day  "
              << "Uruchamia tylko jeden dzien symulacji (tryb testowy)\n"
              << "Argumenty generatora petentow:\n"
              << "  --gen-min-delay <sek>  "
              << "Minimalne opoznienie miedzy petentami, domyslnie 1\n"
              << "  --gen-max-delay <sek>  "
              << "Maksymalne opoznienie miedzy petentami, domyslnie 5\n"
              << "  --gen-max-count <liczba>  "
              << "Maksymalna liczba wygenerowanych petentow (opcjonalnie)\n"
              << "Argumenty urzednika:\n"
              << "  --dept <SC|KM|ML|PD|SA>  "
              << "Wydzial urzednika/petenta\n";
}

struct Config {
    Identity role = Identity::Dyrektor;
    int Tp = 8;
    int Tk = 16;
    int X1 = 2000;
    int X2 = 1000;
    int X3 = 1000;
    int X4 = 1000;
    int X5 = 1000;
    int building_capacity = 100;
    int time_mul = 1000;
    int gen_min_delay_sec = 1;
    int gen_max_delay_sec = 5;
    int gen_max_count = -1;
    bool spawn_generator = false;
    bool one_day = false;
    bool vip = false;
    bool has_child = false;
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
            else if (arg == "--X1" && i + 1 < argc) {
                config.X1 = std::stoi(argv[++i]);
                if (config.X1 < 0) {
                    std::cerr << "Blad: --X1 musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--X2" && i + 1 < argc) {
                config.X2 = std::stoi(argv[++i]);
                if (config.X2 < 0) {
                    std::cerr << "Blad: --X2 musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--X3" && i + 1 < argc) {
                config.X3 = std::stoi(argv[++i]);
                if (config.X3 < 0) {
                    std::cerr << "Blad: --X3 musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--X4" && i + 1 < argc) {
                config.X4 = std::stoi(argv[++i]);
                if (config.X4 < 0) {
                    std::cerr << "Blad: --X4 musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--X5" && i + 1 < argc) {
                config.X5 = std::stoi(argv[++i]);
                if (config.X5 < 0) {
                    std::cerr << "Blad: --X5 musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--N" && i + 1 < argc) {
                config.building_capacity = std::stoi(argv[++i]);
                if (config.building_capacity <= 0) {
                    std::cerr << "Blad: --N musi byc > 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--time-mul" && i + 1 < argc) {
                config.time_mul = std::stoi(argv[++i]);
                if (config.time_mul <= 0) {
                    std::cerr << "Blad: --time-mul musi byc > 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--gen-min-delay" && i + 1 < argc) {
                config.gen_min_delay_sec = std::stoi(argv[++i]);
                if (config.gen_min_delay_sec < 0) {
                    std::cerr << "Blad: --gen-min-delay musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--gen-max-delay" && i + 1 < argc) {
                config.gen_max_delay_sec = std::stoi(argv[++i]);
                if (config.gen_max_delay_sec < 0) {
                    std::cerr << "Blad: --gen-max-delay musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--gen-max-count" && i + 1 < argc) {
                config.gen_max_count = std::stoi(argv[++i]);
                if (config.gen_max_count < 0) {
                    std::cerr << "Blad: --gen-max-count musi byc >= 0\n";
                    return std::nullopt;
                }
            }
            else if (arg == "--gen-from-dyrektor") {
                config.spawn_generator = true;
            }
            else if (arg == "--one-day") {
                config.one_day = true;
            }
            else if (arg == "--vip") {
                config.vip = true;
            }
            else if (arg == "--child") {
                config.has_child = true;
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

        if (config.gen_min_delay_sec > config.gen_max_delay_sec) {
            std::cerr << "Blad: --gen-min-delay nie moze byc wiekszy niz --gen-max-delay\n";
            return std::nullopt;
        }

        return config;
    }
};

int main(int argc, char* argv[]) {
    Logger::set_log_file("./so_projekt.log");
    auto config = Config::parse_arguments(argc, argv);
    if (!config) {
        print_usage(argv[0]);
        return 1;
    }

    if (config->role == Identity::Dyrektor) {
        Logger::clear_log();
    }

    Logger::log(LogSeverity::Debug, config->role, "Config:" 
        " Tp=" + std::to_string(config->Tp) +
        " Tk=" + std::to_string(config->Tk) +
        " N=" + std::to_string(config->building_capacity) +
        " X1=" + std::to_string(config->X1) +
        " X2=" + std::to_string(config->X2) +
        " X3=" + std::to_string(config->X3) +
        " X4=" + std::to_string(config->X4) +
        " X5=" + std::to_string(config->X5) +
        " time_mul=" + std::to_string(config->time_mul) +
        " gen_min_delay=" + std::to_string(config->gen_min_delay_sec) +
        " gen_max_delay=" + std::to_string(config->gen_max_delay_sec) +
        " gen_max_count=" + std::to_string(config->gen_max_count) +
        " gen_from_dyrektor=" + std::to_string(config->spawn_generator) +
        " one_day=" + std::to_string(config->one_day)
    );

    switch (config->role) {
        case Identity::Dyrektor:
        {
            std::array<uint32_t, 5> department_limits = {
                static_cast<uint32_t>(config->X1),
                static_cast<uint32_t>(config->X2),
                static_cast<uint32_t>(config->X3),
                static_cast<uint32_t>(config->X4),
                static_cast<uint32_t>(config->X5)
            };
            dyrektor_main({config->Tp, config->Tk}, department_limits, config->time_mul,
                          config->gen_min_delay_sec, config->gen_max_delay_sec, config->gen_max_count,
                          config->spawn_generator, config->one_day, config->building_capacity);
            break;
        }
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
            if (!config->urzednik_role) {
                Logger::log(LogSeverity::Err, Identity::Petent, "Brak parametru --dept.");
                return 1;
            }
            petent_main(*config->urzednik_role, config->vip, config->has_child);
            break;
        case Identity::Generator:
            generator_main(config->gen_min_delay_sec, config->gen_max_delay_sec, config->time_mul,
                           config->gen_max_count);
            break;
        case Identity::Kasa:
            kasa_main();
            break;
    }

    Logger::log(LogSeverity::Debug, config->role, "Koniec dzialania procesu.");
    return 0;
}

#ifndef SO_PROJEKT_COMMON_H
#define SO_PROJEKT_COMMON_H

#include <cstdint>
#include <optional>
#include <random>
#include <utility>
#include <string>
#include <string_view>
#include <stdexcept>

typedef std::pair<short, short> HoursOpen; // tp, tk

constexpr int TIME_MUL = 1000;

enum class Identity { Petent, Urzednik, Dyrektor, Rejestracja, Generator };

inline std::optional<Identity> string_to_identity(std::string_view str) {
    if (str == "petent") return Identity::Petent;
    if (str == "urzednik") return Identity::Urzednik;
    if (str == "dyrektor") return Identity::Dyrektor;
    if (str == "rejestracja") return Identity::Rejestracja;
    if (str == "generator") return Identity::Generator;
    return std::nullopt;
}

enum class UrzednikRole : uint8_t { SC, KM, ML, PD, SA };

inline std::optional<UrzednikRole> string_to_urzednik_role(std::string_view str) {
    if (str == "SC") return UrzednikRole::SC;
    if (str == "KM") return UrzednikRole::KM;
    if (str == "ML") return UrzednikRole::ML;
    if (str == "PD") return UrzednikRole::PD;
    if (str == "SA") return UrzednikRole::SA;
    return std::nullopt;
}

enum class OfficeStatus { Open, Closed };

struct SharedState {
    uint32_t day;
    uint32_t building_capacity; // N
    uint32_t current_queue_length;
    uint8_t ticket_machines_num;
    uint32_t ticket_counters[5];
    uint32_t simulated_time; // Essentially ticks (which can be affected by TIME_MUL)
    OfficeStatus office_status;

    SharedState(uint32_t capacity) :
        day(0), building_capacity(capacity), current_queue_length(0), ticket_machines_num(1),
        ticket_counters{0, 0, 0, 0, 0},
        simulated_time(0), office_status(OfficeStatus::Closed) {}
};

struct TicketRequestMsg {
    uint32_t petent_id;
    uint8_t is_vip; // boolean
    uint8_t has_child; // boolean
    uint16_t padding; // for explicit alignment
};

struct TicketIssuedMsg {
    uint32_t petent_id;
    uint32_t ticket_number;
    UrzednikRole department; // uint8_t
    uint8_t redirected_from_sa; // boolean
    uint16_t padding; // for explicit alignment
};

namespace rng {
    inline int random_int(int min_inclusive, int max_inclusive) {
        static thread_local std::mt19937 engine(std::random_device{}());
        std::uniform_int_distribution<int> dist(min_inclusive, max_inclusive);
        return dist(engine);
    }
}

#endif // SO_PROJEKT_COMMON_H

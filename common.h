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

enum class Identity { Petent, Urzednik, Dyrektor, Rejestracja };

inline std::optional<Identity> string_to_identity(std::string_view str) {
    if (str == "petent") return Identity::Petent;
    if (str == "urzednik") return Identity::Urzednik;
    if (str == "dyrektor") return Identity::Dyrektor;
    if (str == "rejestracja") return Identity::Rejestracja;
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
    unsigned int day;
    unsigned int building_capacity; // N
    unsigned int current_queue_length;
    short ticket_machines_num;
    unsigned int simulated_time; // Essentially ticks (which can be affected by TIME_MUL)
    OfficeStatus office_status;

    SharedState(unsigned int capacity) :
        day(0), building_capacity(capacity), current_queue_length(0), ticket_machines_num(1),
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

#ifndef SO_PROJEKT_COMMON_H
#define SO_PROJEKT_COMMON_H

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <utility>
#include <string>
#include <string_view>
#include <stdexcept>

typedef std::pair<short, short> HoursOpen; // tp, tk

enum class Identity { Petent, Urzednik, Dyrektor, Rejestracja, Generator, Kasa };

inline std::optional<Identity> string_to_identity(std::string_view str) {
    if (str == "petent") return Identity::Petent;
    if (str == "urzednik") return Identity::Urzednik;
    if (str == "dyrektor") return Identity::Dyrektor;
    if (str == "rejestracja") return Identity::Rejestracja;
    if (str == "generator") return Identity::Generator;
    if (str == "kasa") return Identity::Kasa;
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

inline std::optional<std::string_view> urzednik_role_to_string(UrzednikRole role) {
    if (role == UrzednikRole::SC) return "SC";
    if (role == UrzednikRole::KM) return "KM";
    if (role == UrzednikRole::ML) return "ML";
    if (role == UrzednikRole::PD) return "PD";
    if (role == UrzednikRole::SA) return "SA";
    return std::nullopt;
}

enum class OfficeStatus: bool { Open, Closed };

enum class TicketRejectReason : uint8_t { None, OfficeClosed, LimitReached };

struct SharedState {
    uint32_t day;
    uint32_t building_capacity; // N
    uint32_t current_queue_length;
    uint8_t ticket_machines_num;
    uint32_t ticket_limits[5];
    uint32_t ticket_counters[5];
    uint32_t simulated_time; // Essentially ticks (which can be affected by time_mul)
    uint32_t time_mul;
    OfficeStatus office_status;

    SharedState(uint32_t capacity, const std::array<uint32_t, 5>& limits, uint32_t time_mul_value) :
        day(0), building_capacity(capacity), current_queue_length(0), ticket_machines_num(1),
        ticket_limits{limits[0], limits[1], limits[2], limits[3], limits[4]},
        ticket_counters{0, 0, 0, 0, 0},
        simulated_time(0), time_mul(time_mul_value), office_status(OfficeStatus::Closed) {}
};

struct TicketRequestMsg {
    uint32_t petent_id;
    UrzednikRole department; // uint8_t
    uint8_t is_vip; // boolean
    uint8_t has_child; // boolean
    uint8_t padding; // for explicit alignment
};

struct TicketIssuedMsg {
    uint32_t petent_id;
    uint32_t ticket_number;
    UrzednikRole department; // uint8_t
    uint8_t redirected_from_sa; // boolean
    TicketRejectReason reject_reason; // uint8_t
    uint8_t is_vip; // boolean - priority queue flag
};

enum class ServiceAction : uint8_t { Complete, GoToKasa };

struct ServiceDoneMsg {
    uint32_t petent_id;
    UrzednikRole department; // uint8_t
    ServiceAction action;
    uint8_t padding[2]; // for explicit alignment
};

// Sent by petent to kasa queue, and back to urzednik queue
struct KasaRequestMsg {
    uint32_t petent_id;
    UrzednikRole department; // uint8_t
    uint8_t padding[3]; // for explicit alignment
};

// Priority mtype values for department queues
// Urzednik processes use msgrcv(..., -kNormalQueueType, 0) to dequeue VIP first
constexpr long kVipQueueType = 1;
constexpr long kNormalQueueType = 2;
constexpr long kTicketRequestType = 1;
constexpr long kKasaReturnQueueType = 3; // returning petents
constexpr long kKasaRequestType = 1; // payment requests

namespace rng {
    inline int random_int(int min_inclusive, int max_inclusive) {
        static thread_local std::mt19937 engine(std::random_device{}());
        std::uniform_int_distribution<int> dist(min_inclusive, max_inclusive);
        return dist(engine);
    }
}

#endif // SO_PROJEKT_COMMON_H

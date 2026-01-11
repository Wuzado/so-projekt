#ifndef SO_PROJEKT_COMMON_H
#define SO_PROJEKT_COMMON_H

#include <utility>

typedef std::pair<short, short> HoursOpen; // tp, tk

constexpr int TIME_MUL = 1000;

enum class Identity { Petent, Urzednik, Dyrektor, Rejestracja };

enum class UrzednikRole { SC, KM, ML, PD, SA };

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

#endif // SO_PROJEKT_COMMON_H

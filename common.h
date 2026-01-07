#ifndef SO_PROJEKT_COMMON_H
#define SO_PROJEKT_COMMON_H

enum class Identity { Petent, Urzednik, Dyrektor, Rejestracja };

enum class UrzednikRole { SC, KM, ML, PD, SA };

enum class OfficeStatus { Open, Closed };

struct SharedState {
    unsigned int building_capacity;
    unsigned int current_queue_length;
    unsigned short int ticket_machines_num;
    unsigned int simulated_time; // Time in seconds
    OfficeStatus office_status;
};

void init_shared_state(SharedState& state, unsigned int capacity) {
    state.building_capacity = capacity;
    state.current_queue_length = 0;
    state.ticket_machines_num = 1;
    state.simulated_time = 0; 
    state.office_status = OfficeStatus::Closed;
}

constexpr int TIME_MUL = 1000;

#endif // SO_PROJEKT_COMMON_H

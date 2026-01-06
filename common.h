#ifndef SO_PROJEKT_COMMON_H
#define SO_PROJEKT_COMMON_H

enum class OfficeStatus {
    Open,
    Closed
};

struct SharedState {
    unsigned int building_capacity;
    unsigned int current_queue_length;
    unsigned short int ticket_machines_num;
    unsigned int simulated_time; // Time in seconds
    OfficeStatus office_status;
};

constexpr int TIME_MUL = 1000;

enum IDENTITY {
    PETENT,
    URZEDNIK,
    DYREKTOR,
    REJESTRACJA
};

enum URZEDNIK_ROLE {
    ROLE_SC,
    ROLE_KM,
    ROLE_ML,
    ROLE_PD,
    ROLE_SA
};

#endif //SO_PROJEKT_COMMON_H

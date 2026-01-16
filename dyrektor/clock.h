#ifndef SO_PROJEKT_DYREKTOR_CLOCK_H
#define SO_PROJEKT_DYREKTOR_CLOCK_H

#include "../common.h"
#include <atomic>
#include <pthread.h>

extern std::atomic<bool> simulation_running;

void init_clock(SharedState* state, HoursOpen hours_open);
int start_clock(SharedState* shared_state, HoursOpen hours_open, pthread_t* out_thread);

int stop_clock(pthread_t thread);

#endif // SO_PROJEKT_DYREKTOR_CLOCK_H
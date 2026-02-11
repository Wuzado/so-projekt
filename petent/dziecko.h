#ifndef SO_PROJEKT_DZIECKO_H
#define SO_PROJEKT_DZIECKO_H

#include <csignal>
#include <pthread.h>
#include <sys/types.h>

// Shared state between parent and child
struct ChildThreadData {
    pthread_mutex_t mutex;
    pthread_cond_t cond; // signal when done
    bool done;
    pid_t parent_pid;
    volatile sig_atomic_t* evacuating; // pointer to parent's evacuation flag
};

int child_init(ChildThreadData* data, pid_t parent_pid, volatile sig_atomic_t* evacuating);

int child_start(ChildThreadData* data, pthread_t* thread);

void child_signal_done(ChildThreadData* data);

void child_join_and_cleanup(ChildThreadData* data, pthread_t thread);

#endif // SO_PROJEKT_DZIECKO_H

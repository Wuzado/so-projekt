#ifndef SO_PROJEKT_IPCUTILS_H
#define SO_PROJEKT_IPCUTILS_H

#include <cstddef>
#include <cstdio>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include "common.h"

namespace ipc {

    constexpr const char* IPC_LOCK_FILE = "/tmp/so_projekt_ipc.lock"; // Must be created by Director at startup!

    enum class KeyType : int {
        SharedState = 'S',
        SemaphoreSet = 'M',
        MsgQueueSA = 'A',
        MsgQueueSC = 'C',
        MsgQueueKM = 'K',
        MsgQueueML = 'L',
        MsgQueuePD = 'P'
    };

    // Generate SysV IPC key using ftok()
    inline key_t make_key(KeyType key_type) {
        key_t key = ftok(IPC_LOCK_FILE, static_cast<int>(key_type));
        if (key == -1) {
            perror("ftok failed");
            return -1;
        }
        return key;
    }

    // Shared memory
    namespace shm {

        template <typename T>
        int create(key_t key, mode_t permissions = 0660) {
            int shmid = shmget(key, sizeof(T), IPC_CREAT | IPC_EXCL | permissions);
            if (shmid == -1) {
                perror("shmget failed");
                return -1;
            }
            return shmid;
        }

        template <typename T>
        int get(key_t key) {
            int shmid = shmget(key, sizeof(T), 0);
            if (shmid == -1) {
                perror("shmget failed");
                return -1;
            }
            return shmid;
        }

        template <typename T>
        T* attach(int shmid, bool readonly = true) {
            int flags = readonly ? SHM_RDONLY : 0;
            void* addr = shmat(shmid, nullptr, flags);
            if (addr == (void*)-1) {
                perror("shmat failed");
                return nullptr;
            }
            return static_cast<T*>(addr);
        }

        template <typename T>
        int detach(T* ptr) {
            if (shmdt(ptr) == -1) {
                perror("shmdt failed");
                return -1;
            }
            return 0;
        }

        inline int remove(int shmid) {
            if (shmctl(shmid, IPC_RMID, nullptr) == -1) {
                perror("shmctl IPC_RMID failed");
                return -1;
            }
            return 0;
        }

    } // namespace shm

    namespace thread {
        inline int create(pthread_t* thread, void* (*start_routine)(void*), void* arg,
                          const pthread_attr_t* attr = nullptr) {
            if (pthread_create(thread, attr, start_routine, arg) != 0) {
                perror("pthread_create failed");
                return -1;
            }
            return 0;
        }

        inline int join(pthread_t thread, void** retval = nullptr) {
            if (pthread_join(thread, retval) != 0) {
                perror("pthread_join failed");
                return -1;
            }
            return 0;
        }

        inline int detach(pthread_t thread) {
            if (pthread_detach(thread) != 0) {
                perror("pthread_detach failed");
                return -1;
            }
            return 0;
        }

        inline void exit(void* retval = nullptr) {
            pthread_exit(retval);
        }

    } // namespace thread
} // namespace ipc

#endif // SO_PROJEKT_IPCUTILS_H

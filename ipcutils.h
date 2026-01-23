#ifndef SO_PROJEKT_IPCUTILS_H
#define SO_PROJEKT_IPCUTILS_H

#include <cstddef>
#include <cstdio>
#include <cerrno>
#include <ctime>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include "common.h"

namespace ipc {

    constexpr const char* IPC_LOCK_FILE = "/tmp/so_projekt_ipc.lock"; // Must be created by Director at startup!

    enum class KeyType : int {
        SharedState = 'S',
        SemaphoreSet = 'M',
        MsgQueueRejestracja = 'R',
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
                int err = errno;
                perror("shmget failed");
                errno = err;
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

    // Message queues
    namespace msg {

        template <typename T>
        struct MsgEnvelope {
            long mtype;
            T data;
        };

        inline int create(key_t key, mode_t permissions = 0660) {
            int msqid = msgget(key, IPC_CREAT | IPC_EXCL | permissions);
            if (msqid == -1) {
                int err = errno;
                perror("msgget failed");
                errno = err;
                return -1;
            }
            return msqid;
        }

        inline int get(key_t key) {
            int msqid = msgget(key, 0);
            if (msqid == -1) {
                perror("msgget failed");
                return -1;
            }
            return msqid;
        }

        template <typename T>
        int send(int msqid, long msg_type, const T& data, int flags = 0) {
            MsgEnvelope<T> msg{msg_type, data};
            if (msgsnd(msqid, &msg, sizeof(T), flags) == -1) {
                perror("msgsnd failed");
                return -1;
            }
            return 0;
        }

        template <typename T>
        int receive(int msqid, long msg_type, T* data, int flags = 0) {
            MsgEnvelope<T> msg{};
            if (msgrcv(msqid, &msg, sizeof(T), msg_type, flags) == -1) {
                if (errno == EINTR) {
                    return -1;
                }
                if (!(flags & IPC_NOWAIT && errno == ENOMSG)) {
                    perror("msgrcv failed");
                }
                return -1;
            }
            if (data != nullptr) {
                *data = msg.data;
            }
            return 0;
        }

        inline int remove(int msqid) {
            if (msgctl(msqid, IPC_RMID, nullptr) == -1) {
                perror("msgctl IPC_RMID failed");
                return -1;
            }
            return 0;
        }

    } // namespace msg

    // Semaphores
    namespace sem {

        // POSIX.1 expects a semun union for semctl
        // See: man semctl(2)
        union Semun {
            int val;
            struct semid_ds* buf;
            unsigned short* array;
        };

        inline int create(key_t key, int nsems = 1, mode_t permissions = 0660) {
            int semid = semget(key, nsems, IPC_CREAT | IPC_EXCL | permissions);
            if (semid == -1) {
                int err = errno;
                perror("semget failed");
                errno = err;
                return -1;
            }
            return semid;
        }

        inline int get(key_t key, int nsems = 1) {
            int semid = semget(key, nsems, 0);
            if (semid == -1) {
                perror("semget failed");
                return -1;
            }
            return semid;
        }

        inline int set_val(int semid, int sem_num, int value) {
            Semun arg{};
            arg.val = value;
            if (semctl(semid, sem_num, SETVAL, arg) == -1) {
                perror("semctl SETVAL failed");
                return -1;
            }
            return 0;
        }

        inline int get_val(int semid, int sem_num) {
            int val = semctl(semid, sem_num, GETVAL);
            if (val == -1) {
                perror("semctl GETVAL failed");
                return -1;
            }
            return val;
        }

        inline int set_all(int semid, unsigned short* values) {
            Semun arg{};
            arg.array = values;
            if (semctl(semid, 0, SETALL, arg) == -1) {
                perror("semctl SETALL failed");
                return -1;
            }
            return 0;
        }

        inline int op(int semid, unsigned short sem_num, short sem_op, short sem_flg = 0) {
            sembuf sb{};
            sb.sem_num = sem_num;
            sb.sem_op = sem_op;
            sb.sem_flg = sem_flg;
            if (semop(semid, &sb, 1) == -1) {
                perror("semop failed");
                return -1;
            }
            return 0;
        }

        inline int wait(int semid, unsigned short sem_num, short sem_flg = 0) {
            return op(semid, sem_num, -1, sem_flg);
        }

        inline int post(int semid, unsigned short sem_num, short sem_flg = 0) { return op(semid, sem_num, 1, sem_flg); }

        inline int remove(int semid) {
            if (semctl(semid, 0, IPC_RMID) == -1) {
                perror("semctl IPC_RMID failed");
                return -1;
            }
            return 0;
        }

    } // namespace sem

    namespace mutex {
        inline int init(pthread_mutex_t* mutex, bool process_shared = false) {
            pthread_mutexattr_t attr;
            int rc = pthread_mutexattr_init(&attr);
            if (rc != 0) {
                errno = rc;
                perror("pthread_mutexattr_init failed");
                return -1;
            }

            if (process_shared) {
                rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
                if (rc != 0) {
                    errno = rc;
                    perror("pthread_mutexattr_setpshared failed");
                    pthread_mutexattr_destroy(&attr);
                    return -1;
                }
            }

            rc = pthread_mutex_init(mutex, &attr);
            if (rc != 0) {
                errno = rc;
                perror("pthread_mutex_init failed");
                pthread_mutexattr_destroy(&attr);
                return -1;
            }

            rc = pthread_mutexattr_destroy(&attr);
            if (rc != 0) {
                errno = rc;
                perror("pthread_mutexattr_destroy failed");
                return -1;
            }

            return 0;
        }

        inline int lock(pthread_mutex_t* mutex) {
            int rc = pthread_mutex_lock(mutex);
            if (rc != 0) {
                errno = rc;
                perror("pthread_mutex_lock failed");
                return -1;
            }
            return 0;
        }

        inline int trylock(pthread_mutex_t* mutex) {
            int rc = pthread_mutex_trylock(mutex);
            if (rc == EBUSY) {
                return 1;
            }
            if (rc != 0) {
                errno = rc;
                perror("pthread_mutex_trylock failed");
                return -1;
            }
            return 0;
        }

        inline int unlock(pthread_mutex_t* mutex) {
            int rc = pthread_mutex_unlock(mutex);
            if (rc != 0) {
                errno = rc;
                perror("pthread_mutex_unlock failed");
                return -1;
            }
            return 0;
        }

        inline int destroy(pthread_mutex_t* mutex) {
            int rc = pthread_mutex_destroy(mutex);
            if (rc != 0) {
                errno = rc;
                perror("pthread_mutex_destroy failed");
                return -1;
            }
            return 0;
        }
    } // namespace mutex

    namespace cond {
        inline int init(pthread_cond_t* cond, bool process_shared = false) {
            pthread_condattr_t attr;
            int rc = pthread_condattr_init(&attr);
            if (rc != 0) {
                errno = rc;
                perror("pthread_condattr_init failed");
                return -1;
            }

            if (process_shared) {
                rc = pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
                if (rc != 0) {
                    errno = rc;
                    perror("pthread_condattr_setpshared failed");
                    pthread_condattr_destroy(&attr);
                    return -1;
                }
            }

            rc = pthread_cond_init(cond, &attr);
            if (rc != 0) {
                errno = rc;
                perror("pthread_cond_init failed");
                pthread_condattr_destroy(&attr);
                return -1;
            }

            rc = pthread_condattr_destroy(&attr);
            if (rc != 0) {
                errno = rc;
                perror("pthread_condattr_destroy failed");
                return -1;
            }

            return 0;
        }

        inline int wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
            int rc = pthread_cond_wait(cond, mutex);
            if (rc != 0) {
                errno = rc;
                perror("pthread_cond_wait failed");
                return -1;
            }
            return 0;
        }

        inline int timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const timespec* abs_timeout) {
            int rc = pthread_cond_timedwait(cond, mutex, abs_timeout);
            if (rc == ETIMEDOUT) {
                return 1;
            }
            if (rc != 0) {
                errno = rc;
                perror("pthread_cond_timedwait failed");
                return -1;
            }
            return 0;
        }

        inline int signal(pthread_cond_t* cond) {
            int rc = pthread_cond_signal(cond);
            if (rc != 0) {
                errno = rc;
                perror("pthread_cond_signal failed");
                return -1;
            }
            return 0;
        }

        inline int broadcast(pthread_cond_t* cond) {
            int rc = pthread_cond_broadcast(cond);
            if (rc != 0) {
                errno = rc;
                perror("pthread_cond_broadcast failed");
                return -1;
            }
            return 0;
        }

        inline int destroy(pthread_cond_t* cond) {
            int rc = pthread_cond_destroy(cond);
            if (rc != 0) {
                errno = rc;
                perror("pthread_cond_destroy failed");
                return -1;
            }
            return 0;
        }
    } // namespace cond

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

        inline void exit(void* retval = nullptr) { pthread_exit(retval); }

    } // namespace thread
} // namespace ipc

namespace ipc::helper {
    inline int create_or_reset_shm(key_t key) {
        int shm_id = shm::create<SharedState>(key);
        if (shm_id != -1) {
            return shm_id;
        }
        if (errno != EEXIST) {
            return -1;
        }
        int old_id = shm::get<SharedState>(key);
        if (old_id != -1) {
            shm::remove(old_id);
        }
        return shm::create<SharedState>(key);
    }

    inline int create_or_reset_msg(key_t key) {
        int msg_id = msg::create(key);
        if (msg_id != -1) {
            return msg_id;
        }
        if (errno != EEXIST) {
            return -1;
        }
        int old_id = msg::get(key);
        if (old_id != -1) {
            msg::remove(old_id);
        }
        return msg::create(key);
    }

    inline int create_or_reset_sem(key_t key, int nsems) {
        int sem_id = sem::create(key, nsems);
        if (sem_id != -1) {
            return sem_id;
        }
        if (errno != EEXIST) {
            return -1;
        }
        int old_id = sem::get(key, nsems);
        if (old_id != -1) {
            sem::remove(old_id);
        }
        return sem::create(key, nsems);
    }

    inline KeyType role_to_key(UrzednikRole role) {
        switch (role) {
            case UrzednikRole::SA:
                return KeyType::MsgQueueSA;
            case UrzednikRole::SC:
                return KeyType::MsgQueueSC;
            case UrzednikRole::KM:
                return KeyType::MsgQueueKM;
            case UrzednikRole::ML:
                return KeyType::MsgQueueML;
            case UrzednikRole::PD:
                return KeyType::MsgQueuePD;
            default:
                return KeyType::MsgQueueSA;
        }
    }

    inline int get_role_queue(UrzednikRole role) {
        key_t key = make_key(role_to_key(role));
        if (key == -1) {
            return -1;
        }
        return msg::get(key);
    }

    inline SharedState* get_shared_state(bool readonly, int* shm_id_out = nullptr) {
        key_t key = make_key(KeyType::SharedState);
        if (key == -1) {
            return nullptr;
        }
        int shm_id = shm::get<SharedState>(key);
        if (shm_id == -1) {
            return nullptr;
        }
        auto shared_state = shm::attach<SharedState>(shm_id, readonly);
        if (!shared_state) {
            return nullptr;
        }
        if (shm_id_out != nullptr) {
            *shm_id_out = shm_id;
        }
        return shared_state;
    }

    inline int get_semaphore_set(int nsems = 2) {
        key_t key = make_key(KeyType::SemaphoreSet);
        if (key == -1) {
            return -1;
        }
        return sem::get(key, nsems);
    }

    inline int get_msg_queue(KeyType type) {
        key_t key = make_key(type);
        if (key == -1) {
            return -1;
        }
        return msg::get(key);
    }
} // namespace ipc::helper


#endif // SO_PROJEKT_IPCUTILS_H

#include <time.h>

#include "utils/lock.h"
#include "utils/logger.h"
#include "utils/exception.h"

namespace tube {
namespace utils {

Mutex::Mutex()
{
    pthread_mutex_init(&mutex_, NULL);
}

Mutex::~Mutex()
{
    pthread_mutex_destroy(&mutex_);
}

void
Mutex::lock()
{
    int res = pthread_mutex_lock(&mutex_);
    if (res != 0) {
        throw SyscallException();
    }
}

bool
Mutex::unlock()
{
    // suppress the unlock check here.  BSD will always return an error if
    // mutex is unlocked by a different thread.
    return pthread_mutex_unlock(&mutex_) == 0;
}

bool
Mutex::try_lock()
{
    int res = pthread_mutex_trylock(&mutex_);
    if (res != 0 && res != EBUSY) {
        throw SyscallException();
    }
    return res == 0;
}

Lock::Lock(Mutex& mutex, bool auto_lock)
    : mutex_(mutex), locked_(false)
{
    if (auto_lock) {
        lock();
    }
}

Lock::~Lock()
{
    if (locked_) {
        unlock();
    }
}

void
Lock::lock()
{
    mutex_.lock();
    locked_ = true;
}

bool
Lock::unlock()
{
    if (mutex_.unlock()) {
        locked_ = false;
        return true;
    }
    return false;
}

bool
Lock::try_lock()
{
    if (mutex_.try_lock()) {
        locked_ = true;
        return true;
    }
    return false;
}

Condition::Condition()
{
    pthread_cond_init(&cond_, NULL);
}

Condition::~Condition()
{
    pthread_cond_destroy(&cond_);
}

void
Condition::notify_one()
{
    if (pthread_cond_signal(&cond_) != 0) {
        throw SyscallException();
    }
}

void
Condition::notify_all()
{
    if (pthread_cond_broadcast(&cond_) != 0) {
        throw SyscallException();
    }
}

void
Condition::wait(Lock& lk)
{
    wait(lk.mutex());
}

void
Condition::wait(Mutex& mutex)
{
    if (pthread_cond_wait(&cond_, mutex.pthread_mutex()) != 0) {
        throw SyscallException();
    }
}

bool
Condition::timed_wait(Lock& lk, int timeout_msec)
{
    return timed_wait(lk.mutex(), timeout_msec);
}

bool
Condition::timed_wait(Mutex& mutex, int timeout_msec)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    now.tv_sec += timeout_msec / 1000;
    now.tv_nsec += (timeout_msec % 1000) * 1000000;
    int res = pthread_cond_timedwait(&cond_, mutex.pthread_mutex(), &now);
    if (res != 0 && res != ETIMEDOUT) {
        throw SyscallException();
    }
    return res == 0;
}

}
}

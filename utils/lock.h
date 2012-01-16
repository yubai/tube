// -*- mode: c++ -*-
#ifndef _LOCK_H_
#define _LOCK_H_

#include <pthread.h>

#include "utils/misc.h"

namespace tube {
namespace utils {

class Mutex : public Noncopyable
{
    pthread_mutex_t mutex_;
public:
    Mutex();
    ~Mutex(); // no virtual destructor for performance concern

    void lock();
    bool unlock();
    bool try_lock();

    pthread_mutex_t* pthread_mutex() { return &mutex_; }
};

class Lock : public Noncopyable
{
    Mutex& mutex_;
    bool locked_;
public:
    Lock(Mutex& mutex, bool auto_lock = true);
    ~Lock();

    void lock();
    bool unlock();
    bool try_lock();

    Mutex& mutex() { return mutex_; }
};

class Condition : public Noncopyable
{
    pthread_cond_t cond_;
public:
    Condition();
    ~Condition();

    void wait(Lock& lk);
    void wait(Mutex& lk);

    bool timed_wait(Lock& lk, int timeout_msec);
    bool timed_wait(Mutex& lk, int timeout_msec);

    void notify_one();
    void notify_all();
};

}
}

#endif /* _LOCK_H_ */

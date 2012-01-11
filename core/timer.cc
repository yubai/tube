#include "pch.h"

#include "core/timer.h"

namespace tube {

// class TimerLock
// {
//     utils::Mutex& mutex_;
//     bool          nolock_;
// public:
//     TimerLock(utils::Mutex& mutex, bool nolock)
//         : mutex_(mutex), nolock_(nolock) {
//         if (!nolock_) mutex_.lock();
//     }
//
//     ~TimerLock() {
//         if (!nolock_) mutex_.unlock();
//     }
// };

bool
Timer::TimerKey::operator<(const TimerKey& rhs) const
{
    return (unit < rhs.unit)
        || (unit == rhs.unit && (long) ctx < (long) rhs.ctx);
}

bool
Timer::TimerKey::operator==(const TimerKey& rhs) const
{
    return (unit == rhs.unit && (long) ctx == (long) rhs.ctx);
}

int Timer::kUnitGran = 2; // 2 seconds

Timer::Unit
Timer::current_timer_unit()
{
    return time(NULL) / kUnitGran;
}

Timer::Timer()
    : last_executed_(current_timer_unit())
{
}

bool
Timer::set(Timer::Unit unit, Timer::Context ctx, const Timer::Callback& call)
{
    // TimerLock lk(mutex_, nolock_);
    TimerKey key(unit, ctx);
    if (rbtree_.find(key) != rbtree_.end()) {
        return false;
    }
    rbtree_.insert(std::make_pair(key, call));
    return true;
}

void
Timer::replace(Timer::Unit unit, Timer::Context ctx,
               const Timer::Callback& call)
{
    // utils::Lock lk(mutex_);
    rbtree_.insert(std::make_pair(TimerKey(unit, ctx), call));
}

bool
Timer::remove(Timer::Unit unit, Timer::Context ctx)
{
    // TimerLock lk(mutex_, nolock_);
    TimerTree::iterator it = rbtree_.find(TimerKey(unit, ctx));
    if (it == rbtree_.end()) {
        return false;
    }
    rbtree_.erase(it);
    return true;
}

bool
Timer::query(Unit unit, Context ctx, Callback& call)
{
    // TimerLock lk(mutex_, nolock_);
    TimerTree::iterator it = rbtree_.find(TimerKey(unit, ctx));
    if (it == rbtree_.end()) {
        return false;
    }
    call = it->second;
    return true;
}

bool
Timer::invoke_callback(TimerTree::iterator it)
{
    return it->second(it->first.ctx);
}

void
Timer::process_callbacks()
{
    Unit current_unit = current_timer_unit();
    std::vector<TimerTree::iterator> garbage;
    // utils::Lock lk(mutex_);
    // nolock_ = true;
    for (TimerTree::iterator it = rbtree_.begin(); it != rbtree_.end(); ++it) {
        if (it->first.unit > current_unit) {
            break;
        }
        if (invoke_callback(it)) {
            garbage.push_back(it);
        }
    }
    for (size_t i = 0; i < garbage.size(); i++) {
        rbtree_.erase(garbage[i]);
    }
    // nolock_ = false;
}

void
Timer::dump_all() const
{
    for (TimerTree::const_iterator it = rbtree_.begin(); it != rbtree_.end(); ++it) {
        fprintf(stderr, "timer obj: %d %p\n", it->first.unit, it->first.ctx);
    }
}

}

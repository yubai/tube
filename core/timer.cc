#include "pch.h"

#include "core/timer.h"

namespace tube {

bool
Timer::TimerKey::operator<(const TimerKey& rhs) const
{
    return (unit < rhs.unit && (long) ctx < (long) rhs.ctx);
}

int Timer::kUnitGrand = 2; // 2 seconds

Timer::Unit
Timer::current_timer_unit()
{
    return time(NULL) / kUnitGrand;
}

bool
Timer::set(Timer::Unit unit, Timer::Context ctx, const Timer::Callback& call)
{
    TimerKey key(unit, ctx);
    if (rbtree_.find(key) != rbtree_.end()) {
        return false;
    }
    rbtree_.insert(std::make_pair(key, call));
    return true;
}

void
Timer::replace(Timer::Unit unit, Timer::Context ctx, const Timer::Callback& call)
{
    rbtree_.insert(std::make_pair(TimerKey(unit, ctx), call));
}

bool
Timer::remove(Timer::Unit unit, Timer::Context ctx, Callback& call)
{
    TimerTree::iterator it = rbtree_.find(TimerKey(unit, ctx));
    if (it == rbtree_.end()) {
        return false;
    }
    rbtree_.erase(it);
    call = it->second;
    return true;
}

bool
Timer::remove(Timer::Unit unit, Timer::Context ctx)
{
    Callback dummy;
    return remove(unit, ctx, dummy);
}

bool
Timer::query(Unit unit, Context ctx, Callback& call)
{
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
}

}

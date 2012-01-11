// -*- mode: c++ -*-

#ifndef _TIMER_H_
#define _TIMER_H_

#include <boost/function.hpp>
#include <map>
#include <ctime>

#include "utils/misc.h"

namespace tube {

/**
 * Timer object are used for schedule a callback at a certain time.  Timer have
 * a different granularity than second, which is defined as Time::Unit.
 *
 * Timer is not thread safe.
 */
class Timer
{
public:
    typedef time_t Unit;
    typedef void* Context;
    typedef boost::function<bool (Context)> Callback;

    /**
     * Granularity of the timer.
     */
    static int kUnitGran;

    Timer();

    /**
     * Set a callback that will be executed at time unit.
     * @param unit The time in the future.
     * @param ctx Context for callback, a pointer
     * @param call The callback
     * @return True if succeeded, false means a callback was already scheduled
     * at time unit with context ctx.
     */
    bool set(Unit unit, Context ctx, const Callback& call);
    /**
     * Set a callback and replace the original one if exists.
     * @param unit The time in the future.
     * @param ctx Context for callback, a pointer
     * @param call The callback
     */
    void replace(Unit unit, Context ctx, const Callback& call);
    /**
     * Remove a callback on time unit.
     * @param unit The time unit.
     * @param ctx The Context.
     * @return True if succeeded.
     */
    bool remove(Unit unit, Context ctx);
    /**
     * Query a the callback.
     * @param unit The time in the future.
     * @param ctx Context for callback, a pointer
     * @param call The callback to be retured if found.
     * @return True if found, false otherwise.
     */
    bool query(Unit unit, Context ctx, Callback& call);

    /**
     * Process all out of date callback according to current time.
     */
    void process_callbacks();
    /**
     * Get current time unit.
     */
    static Unit current_timer_unit();
    /**
     * Transform system time to Time::Unit.
     */
    static Unit timer_unit_from_time(time_t t) { return t / kUnitGran; }

    /**
     * A timestamp that mark the last time processing callbacks.
     */
    Unit last_executed_time() { return last_executed_; }
    /**
     * Set the timestamp for the last time processing callbacks.
     */
    void set_last_executed_time(Unit last_executed) {
        last_executed_ = last_executed;
    }

    size_t size() const { return rbtree_.size(); }

    void dump_all() const;

private:
    struct TimerKey {
        Unit unit;
        Context ctx;
        bool operator<(const TimerKey& rhs) const;
        bool operator==(const TimerKey& rhs) const;
        TimerKey(Unit timerunit, Context context)
            : unit(timerunit), ctx(context) {}
    };
    typedef std::map<TimerKey, Callback> TimerTree;

    TimerTree    rbtree_;
    Unit         last_executed_;
    // utils::Mutex mutex_;
    // bool         nolock_;

    bool invoke_callback(TimerTree::iterator it);
};

}

#endif /* _TIMER_H_ */

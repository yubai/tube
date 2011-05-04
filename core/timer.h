// -*- mode: c++ -*-

#ifndef _TIMER_H_
#define _TIMER_H_

#include <boost/function.hpp>
#include <map>
#include <ctime>

#include "utils/misc.h"

namespace tube {

class Timer
{
public:
    typedef time_t Unit;
    typedef void* Context;
    typedef boost::function<bool (Context)> Callback;

    static int kUnitGrand;

    Timer();

    bool set(Unit unit, Context ctx, const Callback& call);
    void replace(Unit unit, Context ctx, const Callback& call);
    bool remove(Unit unit, Context ctx);
    bool query(Unit unit, Context ctx, Callback& call);

    void process_callbacks();
    static Unit current_timer_unit();
    static Unit timer_unit_from_time(time_t t) { return t / kUnitGrand; }

    Unit last_executed_time() { return last_executed_; }
    void set_last_executed_time(Unit last_executed) {
        last_executed_ = last_executed;
    }

private:
    struct TimerKey {
        Unit unit;
        Context ctx;
        bool operator<(const TimerKey& rhs) const;
        TimerKey(Unit timerunit, Context context)
            : unit(timerunit), ctx(context) {}
    };
    typedef std::map<TimerKey, Callback> TimerTree;

    TimerTree    rbtree_;
    Unit         last_executed_;
    utils::Mutex mutex_;
    bool         nolock_;

    bool invoke_callback(TimerTree::iterator it);
};

}

#endif /* _TIMER_H_ */

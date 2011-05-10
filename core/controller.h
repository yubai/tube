// -*- mode: c++ -*-

#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include <set>
#include "utils/misc.h"

namespace tube {

class Scheduler;
class Stage;

/**
 * Controller is to control the thread pool size adaptively according to the
 * current queue size.
 */
class Controller
{
    size_t                    last_queue_size_;
    std::set<utils::ThreadId> auto_threads_;
    size_t                    sched_count_;

    Stage*     stage_;
public:
    static utils::TimeMilliseconds kMaxThreadIdle;

    Controller();
    virtual ~Controller() {}

    void set_stage(Stage* stage) { stage_ = stage; }

    bool is_auto_created(utils::ThreadId id);
    bool is_auto_created();
    void auto_create(Scheduler* sched);
};

}

#endif /* _CONTROLLER_H_ */

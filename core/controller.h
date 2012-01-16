// -*- mode: c++ -*-

#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include <set>
#include "utils/misc.h"
#include "utils/lock.h"

namespace tube {

class Stage;

/**
 * Controller is to control the thread pool size adaptively according to the
 * current stage load.
 */
class Controller
{
    utils::Mutex              mutex_;
    std::set<utils::ThreadId> auto_threads_;

    std::vector<long> load_history_;
    size_t            reserve_;
    Stage*            stage_;
    long              current_load_;
    long              current_speed_;
    long              best_speed_;
    size_t            best_threads_size_;
public:
    static int kMaxThreadIdle;
    static int kCheckAutoCreate;

    Controller();
    virtual ~Controller() {}

    void set_stage(Stage* stage) { stage_ = stage; }

    bool is_auto_created(utils::ThreadId id);
    bool is_auto_created();
    void exit_auto_thread(utils::ThreadId id);
    void exit_auto_thread();

    void increase_load(long inc);
    void decrease_load(long dec);

private:
    void check_thread();
    bool check_auto_create();
};

}

#endif /* _CONTROLLER_H_ */

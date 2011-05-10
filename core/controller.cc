#include <cstdio>

#include "core/controller.h"
#include "core/pipeline.h"
#include "core/stages.h"

namespace tube {

static const size_t kSchedAddCount = 100;
static const double kCreateRadio = 0.86;

Controller::Controller()
    : stage_(NULL)
{
    last_queue_size_ = 0;
    sched_count_ = 0;
}

bool
Controller::is_auto_created(utils::ThreadId id)
{
    if (auto_threads_.find(id) != auto_threads_.end()) {
        return true;
    }
    return false;
}

bool
Controller::is_auto_created()
{
    return is_auto_created(boost::this_thread::get_id());
}

utils::TimeMilliseconds
Controller::kMaxThreadIdle = utils::TimeMilliseconds(500);

void
Controller::auto_create(Scheduler* sched)
{
    if (stage_ == NULL) {
        return;
    }
    sched_count_++;
    // only auto create thread when kSchedCount has been exceeded.
    if (sched_count_ < kSchedAddCount) {
        return;
    }
    // TODO: more sophisticated auto create mechanism?
    sched_count_ = 0;
    size_t current_size = sched->size_nolock();
    int delta_size = current_size - last_queue_size_;
    if (1.0 * delta_size > kCreateRadio * kSchedAddCount) {
        printf("%d auto create\n", delta_size);
        auto_threads_.insert(stage_->start_thread());
    }
    last_queue_size_ = current_size;
}

}

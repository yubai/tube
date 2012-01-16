#include <cstdio>
#include <cstdlib>

#include "core/controller.h"
#include "core/pipeline.h"
#include "core/stages.h"
#include "utils/logger.h"

namespace tube {

Controller::Controller()
    : reserve_(0), stage_(NULL), current_load_(0), current_speed_(0),
      best_speed_(0), best_threads_size_(0)
{
    utils::Thread th(boost::bind(&Controller::check_thread, this));
}

bool
Controller::is_auto_created(utils::ThreadId id)
{
    utils::Lock lk(mutex_);
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

void
Controller::exit_auto_thread(utils::ThreadId id)
{
    utils::Lock lk(mutex_);
    auto_threads_.erase(id);
}

void
Controller::exit_auto_thread()
{
    exit_auto_thread(boost::this_thread::get_id());
}

void
Controller::increase_load(long inc)
{
    utils::Lock lk(mutex_);
    current_load_ += inc;
}

void
Controller::decrease_load(long dec)
{
    utils::Lock lk(mutex_);
    current_load_ -= dec;
    current_speed_ += dec;
}

int
Controller::kMaxThreadIdle = 500;

int
Controller::kCheckAutoCreate = 300;

static const size_t
kMaxFeedback = 16;

static const size_t
kMinFeedback = 8;

static const long
kMinLoad = 15;

static const size_t
kMaxThread = 128;

static const size_t
kDecisionHistoryNumber = 16;

bool
Controller::check_auto_create()
{
    utils::Lock lk(mutex_);
    if (load_history_.size() == kMaxFeedback) {
        load_history_.erase(load_history_.begin());
    }
    load_history_.push_back(current_load_);

    size_t at_size = auto_threads_.size();
    size_t lh_size = load_history_.size();

    if (best_speed_ < current_speed_) {
        best_speed_ = current_speed_;
        best_threads_size_ = at_size;
    }
    current_speed_ = 0;

    if (reserve_ > 0) {
        reserve_--;
        return false;
    }

    if (at_size > kMaxThread) {
        return false;
    }

    if (lh_size < kMinFeedback) {
        return false;
    }

    if (best_threads_size_ < at_size) {
        return false;
    }

    long sum_last = 0;
    long sum_now = 0;
    for (size_t i = 0; i < lh_size; i++) {
        long load = load_history_[i];
        if (i < lh_size / 2) {
            sum_last += load;
        } else if (i >= (lh_size + 1) / 2) {
            if (load < kMinLoad) {
                return false;
            }
            sum_now += load;
        }
    }
    if (sum_last > sum_now) {
        return false;
    }
    reserve_ = kMaxFeedback;
    return true;
}

void
Controller::check_thread()
{
    while (true) {
        usleep(kCheckAutoCreate * 1000);
        if (check_auto_create()) {
            LOG(INFO, "server loads high, auto-create a new thread.");
            auto_threads_.insert(stage_->start_thread());
        }
    }
}

}

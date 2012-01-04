#include "pch.h"

#include <cassert>
#include <cstdlib>
#include <limits.h>

#include "utils/exception.h"
#include "utils/logger.h"
#include "utils/misc.h"
#include "core/stages.h"
#include "core/pipeline.h"
#include "core/controller.h"

using namespace tube::utils;

namespace tube {

Stage::Stage(const std::string& name)
    : pipeline_(Pipeline::instance()), thread_pool_size_(1)
{
    sched_ = NULL;
    LOG(DEBUG, "adding %s stage to pipeline", name.c_str());
    pipeline_.add_stage(name, this);
}

bool
Stage::sched_add(Connection* conn)
{
    if (sched_) {
        sched_->add_task(conn);
    }
    return true;
}

void
Stage::sched_remove(Connection* conn)
{
    if (sched_) {
        sched_->remove_task(conn);
    }
}

void
Stage::sched_reschedule()
{
    if (sched_) {
        sched_->reschedule();
    }
}

void
Stage::main_loop()
{
    if (!sched_) return;
    while (true) {
        Connection* conn = sched_->pick_task();
        if (conn == NULL) {
            LOG(INFO, "server loads low, destroy auto-created thread.");
            return;
        }
        if (process_task(conn) >= 0) {
            conn->unlock();
            pipeline_.reschedule_all();
        }
    }
}

ThreadId
Stage::start_thread()
{
    Thread th(boost::bind(&Stage::main_loop, this));
    return th.get_id();
}

void
Stage::start_thread_pool()
{
    for (size_t i = 0; i < thread_pool_size_; i++) {
        start_thread();
    }
}

PollStage::PollStage(const std::string& name)
    : Stage(name), timeout_(-1), current_poller_(0),
      poller_name_(PollerFactory::instance().default_poller_name())
{
}

void
PollStage::add_poll(Poller* poller)
{
    utils::Lock lk(mutex_);
    pollers_.push_back(poller);
}

int PollInStage::kDefaultTimeout = 10;

PollInStage::PollInStage()
    : PollStage("poll_in")
{
    sched_ = NULL; // no scheduler, need to override ``sched_add``
    timeout_ = kDefaultTimeout; // 10s by default
}

PollInStage::~PollInStage()
{
}

bool
PollInStage::sched_add(Connection* conn)
{
    utils::Lock lk(mutex_);
    current_poller_ = (current_poller_ + 1) % pollers_.size();

    Poller& poller = *pollers_[current_poller_];
    Timer::Callback callback = boost::bind(
        &PollInStage::cleanup_idle_connection_callback, this,
        boost::ref(poller), _1);

    poller.timer().replace(conn->timer_sched_time(), conn, callback);
    return poller.add_fd(conn->fd(), conn, kPollerEventRead | kPollerEventHup
                         | kPollerEventError);
}

void
PollInStage::sched_remove(Connection* conn)
{
    utils::Lock lk(mutex_);
    Timer::Unit oldfuture = conn->timer_sched_time();
    for (size_t i = 0; i < pollers_.size(); i++) {
        if (pollers_[i]->remove_fd(conn->fd())) {
            pollers_[i]->timer().remove(oldfuture, conn);
            return;
        }
    }
}

void
PollInStage::initialize()
{
    parser_stage_ = Pipeline::instance().find_stage("parser");
    if (parser_stage_ == NULL)
        throw std::invalid_argument("cannot find parser stage");
    recycle_stage_ = Pipeline::instance().find_stage("recycle");
    if (recycle_stage_ == NULL)
        throw std::invalid_argument("cannot find recycle stage");
}

void
PollInStage::cleanup_connection(Connection* conn)
{
    assert(conn);

    if (conn->is_active()) {
        conn->set_active(false);
        ::shutdown(conn->fd(), SHUT_RDWR);
        sched_remove(conn);
        recycle_stage_->sched_add(conn);
        // printf("%s %p\n", __FUNCTION__, conn);
    }
}

void
PollInStage::cleanup_connection(Poller& poller, Connection* conn)
{
    assert(conn);

    if (conn->is_active()) {
        Timer::Unit oldfuture = conn->timer_sched_time();
        conn->set_active(false);
        ::shutdown(conn->fd(), SHUT_RDWR);

        poller.remove_fd(conn->fd());
        poller.timer().remove(oldfuture, conn);
        recycle_stage_->sched_add(conn);
        // printf("%s %p\n", __FUNCTION__, conn);
    }
}

bool
PollInStage::cleanup_idle_connection_callback(Poller& poller, void* ptr)
{
    Connection* conn = (Connection*) ptr;
    if (!conn->try_lock())
        return false;
    cleanup_connection(poller, conn);
    conn->unlock();
    return true;
}

void
PollInStage::read_connection(Poller& poller, Connection* conn)
{
    assert(conn);

    if (!conn->try_lock()) // avoid lock contention
        return;

    // update the timer
    Timer& timer = poller.timer();
    Timer::Unit oldfuture = conn->timer_sched_time();
    if (conn->update_last_active()) {
        Timer::Callback cb =
            boost::bind(&PollInStage::cleanup_idle_connection_callback,
                        this, boost::ref(poller), _1);
        utils::Lock lk(mutex_);
        timer.remove(oldfuture, conn);
        timer.replace(conn->timer_sched_time(), conn, cb);
    }

    int nread;
    do {
        nread = conn->in_stream().read_into_buffer();
    } while (nread > 0);

    if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // send it to parser stage
        parser_stage_->sched_add(conn);
    } else {
        // error happened, clean it up
        utils::Lock lk(mutex_);
        cleanup_connection(poller, conn);
    }
    conn->unlock();
    pipeline_.reschedule_all();
}

void
PollInStage::handle_connection(Poller& poller, Connection* conn,
                               PollerEvent evt)
{
    if ((evt & kPollerEventHup) || (evt & kPollerEventError)) {
        if (conn->try_lock()) {
            utils::Lock lk(mutex_);
            cleanup_connection(poller, conn);
            conn->unlock();
        }
    } else if (evt & kPollerEventRead) {
        read_connection(poller, conn);
    }
}

void
PollInStage::post_handle_connection(Poller& poller)
{
    if (mutex_.try_lock()) {
        Timer::Unit current_unit = Timer::current_timer_unit();
        Timer& timer = poller.timer();
        if (timer.last_executed_time() + Timer::timer_unit_from_time(timeout_)
            <= current_unit) {
            poller.timer().process_callbacks();
            timer.set_last_executed_time(current_unit);
        }
        mutex_.unlock();
    }
    recycle_stage_->sched_add(NULL); // add recycle barrier
}

void
PollInStage::main_loop()
{
    Poller* poller = PollerFactory::instance().create_poller(poller_name_);
    Poller::EventCallback evthdl =
        boost::bind(&PollInStage::handle_connection, this, boost::ref(*poller),
                    _1, _2);
    Poller::PollerCallback posthdl =
        boost::bind(&PollInStage::post_handle_connection, this,
                    boost::ref(*poller));

    poller->set_post_handler(posthdl);
    poller->set_event_handler(evthdl);
    add_poll(poller);
    poller->handle_event(timeout_);
    PollerFactory::instance().destroy_poller(poller);
    delete poller;
}

BlockOutStage::BlockOutStage()
    : Stage("write_back")
{
    sched_ = new QueueScheduler(true); // suppress lock
}

BlockOutStage::~BlockOutStage()
{
    delete sched_;
}

bool
BlockOutStage::sched_add(Connection* conn)
{
    conn->set_cork();
    pipeline_.disable_poll(conn);
    utils::set_socket_blocking(conn->fd(), true);
    Stage::sched_add(conn);
    return true;
}

int
BlockOutStage::process_task(Connection* conn)
{
    OutputStream& out = conn->out_stream();
    int rs = out.write_into_output();
    bool has_error = (rs < 0);

    if (!out.is_done() && rs > 0) {
        Stage::sched_add(conn);
        return -1;
    } else {
        conn->clear_cork();
        if (conn->is_close_after_finish() || has_error) {
            conn->active_close();
        } else {
            utils::set_socket_blocking(conn->fd(), false);
            pipeline_.enable_poll(conn);
        }
        return 0; // done, and not to schedule it anymore
    }
}

PollOutStage::PollOutStage()
    : PollStage("write_back")
{}

PollOutStage::~PollOutStage()
{}

bool
PollOutStage::sched_add(Connection* conn)
{
    utils::Lock lk(mutex_);
    current_poller_ = (current_poller_ + 1) % pollers_.size();
    Poller& poller = *pollers_[current_poller_];
    pipeline_.disable_poll(conn);
    conn->set_cork();
    return poller.add_fd(conn->fd(), conn, kPollerEventWrite | kPollerEventHup
                         | kPollerEventError);
}

void
PollOutStage::cleanup_connection(Poller& poller, Connection* conn)
{
    utils::Lock lk(mutex_);
    poller.remove_fd(conn->fd());
    conn->unlock();
    pipeline_.reschedule_all();
}

void
PollOutStage::handle_connection(Poller& poller, Connection* conn,
                                PollerEvent evt)
{
    if ((evt & kPollerEventHup) || (evt & kPollerEventError)) {
        conn->clear_cork();
        conn->active_close();
        cleanup_connection(poller, conn);
    } else {
        OutputStream& out = conn->out_stream();
        int nwrite = 0;
        bool has_error = false;
        do {
            nwrite = out.write_into_output();
        } while (nwrite > 0);

        if (nwrite < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            has_error = true;
        }

        if (out.is_done() || has_error) {
            conn->clear_cork();

            if (conn->has_continuation()) {
                poller.remove_fd(conn->fd());
                conn->resched_continuation();
                return;
            }

            if (conn->is_close_after_finish() || has_error) {
                conn->active_close();
            } else {
                pipeline_.enable_poll(conn);
            }
            cleanup_connection(poller, conn);
        }
    }
}

void
PollOutStage::main_loop()
{
    Poller* poller = PollerFactory::instance().create_poller(poller_name_);
    Poller::EventCallback evthdl =
        boost::bind(&PollOutStage::handle_connection, this, boost::ref(*poller),
                    _1, _2);
    poller->set_event_handler(evthdl);
    add_poll(poller);
    poller->handle_event(timeout_);
    delete poller;
}

ParserStage::ParserStage()
    : Stage("parser")
{
    sched_ = new QueueScheduler();
}

ParserStage::~ParserStage()
{
    delete sched_;
}

RecycleStage::RecycleStage()
    : Stage("recycle"), recycle_batch_size_(1) {}

bool
RecycleStage::sched_add(Connection* conn)
{
    utils::Lock lk(mutex_);
    queue_.push(conn);
    cond_.notify_one();
    return true;
}

void
RecycleStage::start_thread_pool()
{
    // omit the thread pool size setting
    start_thread();
}

void
RecycleStage::main_loop()
{
    Pipeline& pipeline = Pipeline::instance();
    std::vector<Connection*> dead_conns;
    while (true) {
        mutex_.lock();
        while (true) {
            while (queue_.empty()) {
                cond_.wait(mutex_);
            }
            Connection* conn = queue_.front();
            queue_.pop();
            if (conn) {
                dead_conns.push_back(conn);
            } else {
                if (dead_conns.size() > recycle_batch_size_) {
                    break;
                }
            }
        }
        mutex_.unlock();

        for (size_t i = 0; i < dead_conns.size(); i++) {
            pipeline.dispose_connection(dead_conns[i]);
        }
        dead_conns.clear();
    }
}

}

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

int PollStage::kDefaultTimeout = Timer::kUnitGran;

PollStage::PollStage(const std::string& name)
    : Stage(name), timeout_(kDefaultTimeout), current_poller_(0),
      poller_name_(PollerFactory::instance().default_poller_name())
{
}

void
PollStage::add_poll(Poller* poller)
{
    utils::Lock lk(mutex_);
    pollers_.push_back(poller);
}

void
PollStage::update_connection(Poller& poller, Connection* conn,
                             Timer::Callback cb)
{
    Timer& timer = poller.timer();
    Timer::Unit oldfuture = conn->timer_sched_time();
    if (conn->update_last_active()) {
        utils::Lock lk(mutex_);
        timer.remove(oldfuture, conn);
        timer.replace(conn->timer_sched_time(), conn, cb);
    }
}

void
PollStage::trigger_timer_callback(Poller& poller)
{
    if (mutex_.try_lock()) {
        Timer::Unit current_unit = Timer::current_timer_unit();
        Timer& timer = poller.timer();
        if (timer.last_executed_time() + Timer::timer_unit_from_time(timeout_)
            <= current_unit) {
            timer.process_callbacks();
            timer.set_last_executed_time(current_unit);
        }
        mutex_.unlock();
    }
}

size_t
PollInStage::kMaxRecycleCount = 100;

PollInStage::PollInStage()
    : PollStage("poll_in")
{
    sched_ = NULL; // no scheduler, need to override ``sched_add``
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
PollInStage::sched_remove_nolock(Connection* conn, bool recycle)
{
    Timer::Unit oldfuture = conn->timer_sched_time();
    for (size_t i = 0; i < pollers_.size(); i++) {
        if (pollers_[i]->remove_fd(conn->fd())) {
            pollers_[i]->timer().remove(oldfuture, conn);
            if (recycle) {
                pollers_[i]->expired_connections().push_back(conn);
            }
            return;
        }
    }
}

void
PollInStage::sched_remove(Connection* conn)
{
    utils::Lock lk(mutex_);
    sched_remove_nolock(conn, false);
}

void
PollInStage::initialize()
{
    parser_stage_ = Pipeline::instance().find_stage("parser");
    if (parser_stage_ == NULL)
        throw std::invalid_argument("cannot find parser stage");
}

void
PollInStage::cleanup_connection(Connection* conn)
{
    assert(conn);

    if (conn->is_active()) {
        conn->set_active(false);
        ::shutdown(conn->fd(), SHUT_RDWR);
        utils::Lock lk(mutex_);
        sched_remove_nolock(conn, true);
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

        utils::Lock lk(mutex_);
        poller.remove_fd(conn->fd());
        poller.timer().remove(oldfuture, conn);
        poller.expired_connections().push_back(conn);
        // printf("%s %p poller: %d timer: %d\n", __FUNCTION__, conn,
        //        poller.size(), poller.timer().size());
    }
}

bool
PollInStage::cleanup_idle_connection_callback(Poller& poller, void* ptr)
{
    Connection* conn = (Connection*) ptr;
    if (!conn->try_lock())
        return false;
    ::shutdown(conn->fd(), SHUT_RDWR);
    poller.remove_fd(conn->fd());
    poller.expired_connections().push_back(conn);
    conn->unlock();
    return true; // returning tree, so timer will delete this callback
}

void
PollInStage::read_connection(Poller& poller, Connection* conn)
{
    assert(conn);

    if (!conn->try_lock()) // avoid lock contention
        return;

    // update the timer
    update_connection(poller, conn, boost::bind(
                          &PollInStage::cleanup_idle_connection_callback,
                          this, boost::ref(poller), _1));
    int nread;
    do {
        nread = conn->in_stream().read_into_buffer();
    } while (nread > 0);

    if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // send it to parser stage
        parser_stage_->sched_add(conn);
    } else {
        // error happened, clean it up
        cleanup_connection(poller, conn);
    }
    conn->unlock();
    pipeline_.reschedule_all();
}

void
PollInStage::handle_connection(Poller& poller, Connection* conn,
                               PollerEvent evt)
{
    // fprintf(stderr, "%s %p\n", __FUNCTION__, conn);
    if ((evt & kPollerEventHup) || (evt & kPollerEventError)) {
        if (conn->try_lock()) {
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
    trigger_timer_callback(poller);
    if (mutex_.try_lock()) {
        Poller::ExpiredConnectionList& expired = poller.expired_connections();
        Poller::ExpiredConnectionList::iterator it = expired.begin();
        size_t count = 0;
        while (it != expired.end() && count < kMaxRecycleCount) {
            Poller::ExpiredConnectionList::iterator cur = it;
            Connection* conn = *cur;
            ++it;
            if (pipeline_.dispose_connection(conn)) {
                expired.erase(cur);
                count++;
            }
        }
        mutex_.unlock();
    }
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
    conn->update_last_active(); // update the initial timestamp for timeout
    return poller.add_fd(conn->fd(), conn, kPollerEventWrite | kPollerEventHup
                         | kPollerEventError);
}

void
PollOutStage::cleanup_connection(Poller& poller, Connection* conn)
{
    utils::Lock lk(mutex_);
    poller.remove_fd(conn->fd());
    poller.timer().remove(conn->timer_sched_time(), conn);
    conn->unlock();
    pipeline_.reschedule_all();
}

bool
PollOutStage::cleanup_idle_connection_callback(Poller& poller, void* ptr)
{
    Connection* conn = (Connection*) ptr;
    conn->clear_cork();
    conn->active_close();
    poller.remove_fd(conn->fd());
    conn->unlock();
    return true;
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
        update_connection(poller, conn, boost::bind(
                              &PollOutStage::cleanup_idle_connection_callback,
                              this, boost::ref(poller), _1));
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
                utils::Lock lk(mutex_);
                poller.remove_fd(conn->fd());
                poller.timer().remove(conn->timer_sched_time(), conn);
                conn->resched_continuation();
                return;
            }

            if (conn->is_close_after_finish() || has_error
                || !conn->is_active()) {
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
    Poller::PollerCallback posthdl =
        boost::bind(&PollOutStage::trigger_timer_callback, this,
                    boost::ref(*poller));
    poller->set_post_handler(posthdl);
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

}

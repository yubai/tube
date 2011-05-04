#include "pch.h"

#include <cassert>
#include <cstdlib>
#include <limits.h>

#include "utils/exception.h"
#include "utils/logger.h"
#include "utils/misc.h"
#include "core/stages.h"
#include "core/pipeline.h"

using namespace tube::utils;

namespace tube {

Stage::Stage(const std::string& name)
    : pipeline_(Pipeline::instance())
{
    sched_ = NULL;
    LOG(INFO, "adding %s stage to pipeline", name.c_str());
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
        if (process_task(conn) >= 0) {
            conn->unlock();
            pipeline_.reschedule_all();
        }
    }
}

void
Stage::start_thread()
{
    Thread th(boost::bind(&Stage::main_loop, this));
}

int PollInStage::kDefaultTimeout = 10;

PollInStage::PollInStage()
    : Stage("poll_in")
{
    sched_ = NULL; // no scheduler, need to override ``sched_add``
    timeout_ = kDefaultTimeout; // 10s by default
    poller_name_ = PollerFactory::instance().default_poller_name();
    current_poller_ = 0;
}

PollInStage::~PollInStage()
{
    for (size_t i = 0; i < pollers_.size(); i++) {
        PollerFactory::instance().destroy_poller(pollers_[i]);
    }
}

void
PollInStage::add_poll(Poller* poller)
{
    utils::Lock lk(mutex_);
    pollers_.push_back(poller);
}

bool
PollInStage::sched_add(Connection* conn)
{
    utils::Lock lk(mutex_);
    current_poller_ = (current_poller_ + 1) % pollers_.size();
    Poller& poller = *pollers_[current_poller_];
    Timer::Unit current_unit = Timer::current_timer_unit();
    Timer::Unit future_unit = current_unit
        + Timer::timer_unit_from_time(conn->timeout);
    Timer::Callback callback = boost::bind(
        &PollInStage::cleanup_idle_connection_callback, this,
        boost::ref(poller), _1);

    poller.timer().replace(future_unit, conn, callback);
    return poller.add_fd(
        conn->fd, conn, kPollerEventRead | kPollerEventWrite | kPollerEventHup);
}

void
PollInStage::sched_remove(Connection* conn)
{
    utils::Lock lk(mutex_);
    Timer::Unit oldfuture = conn->last_active
        + Timer::timer_unit_from_time(conn->timeout);
    for (size_t i = 0; i < pollers_.size(); i++) {
        if (pollers_[i]->remove_fd(conn->fd)) {
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

    if (!conn->inactive) {
        conn->inactive = true;
        ::shutdown(conn->fd, SHUT_RDWR);
        sched_remove(conn);
        recycle_stage_->sched_add(conn);
    }
}

void
PollInStage::cleanup_connection(Poller& poller, Connection* conn)
{
    assert(conn);

    if (!conn->inactive) {
        Timer::Unit oldfuture = conn->last_active
            + Timer::timer_unit_from_time(conn->timeout);
        conn->inactive = true;
        ::shutdown(conn->fd, SHUT_RDWR);
        poller.remove_fd(conn->fd);
        poller.timer().remove(oldfuture, conn);
        recycle_stage_->sched_add(conn);
    }
}

bool
PollInStage::cleanup_idle_connection_callback(Poller& poller, void* ptr)
{
    Connection* conn = (Connection*) ptr;
    if (!conn->trylock())
        return false;
    cleanup_connection(poller, conn);
    return true;
}

void
PollInStage::read_connection(Poller& poller, Connection* conn)
{
    assert(conn);

    if (!conn->trylock()) // avoid lock contention
        return;

    // update the timer
    Timer& timer = poller.timer();
    Timer::Unit current_unit = Timer::current_timer_unit();
    if (conn->last_active != current_unit) {
        Timer::Unit oldfuture = conn->last_active
            + Timer::timer_unit_from_time(conn->timeout);
        Timer::Unit future = current_unit
            + Timer::timer_unit_from_time(conn->timeout);
        Timer::Callback cb =
            boost::bind(&PollInStage::cleanup_idle_connection_callback,
                        this, boost::ref(poller), _1);
        timer.remove(oldfuture, conn, cb);
        timer.replace(future, conn, cb);
        conn->last_active = current_unit;
    }

    int nread;
    do {
        nread = conn->in_stream.read_into_buffer();
    } while (nread > 0);
    conn->last_active = time(NULL);
    conn->unlock();
    pipeline_.reschedule_all();

    if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // send it to parser stage
        parser_stage_->sched_add(conn);
    } else {
        cleanup_connection(poller, conn);
    }
}

void
PollInStage::handle_connection(Poller& poller, Connection* conn,
                               PollerEvent evt)
{
    if ((evt & kPollerEventHup) || (evt & kPollerEventError)) {
        cleanup_connection(poller, conn);
    } else if (evt & kPollerEventRead) {
        read_connection(poller, conn);
    }
}

void
PollInStage::post_handle_connection(Poller& poller)
{
    Timer::Unit current_unit = Timer::current_timer_unit();
    Timer& timer = poller.timer();
    if (timer.last_executed_time() + Timer::timer_unit_from_time(timeout_)
        <= current_unit) {
        poller.timer().process_callbacks();
        timer.set_last_executed_time(current_unit);
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
    delete poller;
}

WriteBackStage::WriteBackStage()
    : Stage("write_back")
{
    sched_ = new QueueScheduler(true);
}

WriteBackStage::~WriteBackStage()
{
    delete sched_;
}

int
WriteBackStage::process_task(Connection* conn)
{
    conn->last_active = time(NULL);
    utils::set_socket_blocking(conn->fd, true);
    OutputStream& out = conn->out_stream;
    int rs = out.write_into_output();
    utils::set_socket_blocking(conn->fd, false);

    if (!out.is_done() && rs > 0) {
        sched_add(conn);
        return -1;
    } else {
        conn->clear_cork();
        if (conn->close_after_finish) {
            conn->active_close();
        }
        return 0; // done, and not to schedule it anymore
    }
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

bool
RecycleStage::sched_add(Connection* conn)
{
    utils::Lock lk(mutex_);
    queue_.push(conn);
    if (conn == NULL) {
        cond_.notify_one();
    }
    return true;
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

        utils::XLock lk(pipeline.mutex());
        for (size_t i = 0; i < dead_conns.size(); i++) {
            pipeline.dispose_connection(dead_conns[i]);
        }
        dead_conns.clear();
    }
}

}

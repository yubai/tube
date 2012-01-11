#include "pch.h"

#include <ctime>
#include <cassert>
#include <unistd.h>
#include <netinet/tcp.h>

#include "core/pipeline.h"
#include "core/stages.h"
#include "utils/logger.h"
#include "utils/misc.h"

namespace tube {

Connection::Connection(int sock)
    : fd_(sock), timeout_(0), in_stream_(sock), out_stream_(sock),
      last_active_(0), continuation_data_(NULL)
{
    update_last_active();
    flags_ = kFlagCorkEnabled | kFlagActive;
    // set nodelay
    int state = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &state, sizeof(state));
}

Timer::Unit
Connection::timer_sched_time() const
{
    return last_active_ + Timer::timer_unit_from_time(timeout_);
}

bool
Connection::update_last_active()
{
    Timer::Unit current_unit = Timer::current_timer_unit();
    if (last_active_ == current_unit) {
        return false;
    }
    last_active_ = current_unit;
    return true;
}

void
Connection::active_close()
{
    PollInStage* stage = (PollInStage*) Pipeline::instance().poll_in_stage();
    stage->cleanup_connection(this);
}

void
Connection::set_cork()
{
    if (!is_cork_enabled()) return;
#ifdef __linux__
    int state = 1;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_CORK, &state, sizeof(state)) < 0) {
        LOG(WARNING, "Cannot set TCP_CORK on fd %d", fd_);
    }
#endif
}

void
Connection::clear_cork()
{
    if (!is_cork_enabled()) return;
#ifdef __linux__
    int state = 0;
    if (setsockopt(fd_, IPPROTO_TCP, TCP_CORK, &state, sizeof(state)) < 0) {
        LOG(WARNING, "Cannot clear TCP_CORK on fd %d", fd_);
    }
    // ::fsync(fd_);
#endif
}

void
Connection::set_io_timeout(int msec)
{
    struct timeval tm;
    tm.tv_sec = msec / 1000;
    tm.tv_usec = (msec % 1000) * 1000;

    if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tm, sizeof(struct timeval))
        < 0) {
        LOG(WARNING, "Cannot set IO timeout on fd %d", fd_);
    }
    if (setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tm, sizeof(struct timeval))
        < 0) {
        LOG(WARNING, "Cannot set IO timeout on fd %d", fd_);
    }
}

bool
Connection::try_lock()
{
    if (mutex_.try_lock()) {
#ifdef TRACK_OWNER
        owner_ = utils::get_thread_id();
#endif
        return true;
    }
    return false;
}

void
Connection::lock()
{
    mutex_.lock();
#ifdef TRACK_OWNER
    owner_ = utils::get_thread_id();
#endif
}

void
Connection::unlock()
{
#ifdef TRACK_OWNER
    owner_ = -1;
#endif
    mutex_.unlock();
}

std::string
Connection::address_string() const
{
    return address_.address_string();
}

Scheduler::Scheduler()
    : controller_(NULL)
{
}

Scheduler::~Scheduler()
{
}

size_t
QueueScheduler::kMemoryPoolSize = 320 << 10;

QueueScheduler::QueueScheduler(bool suppress_connection_lock)
    : Scheduler(), pool_(kMemoryPoolSize), list_(pool_),
      suppress_connection_lock_(suppress_connection_lock)
{
}

void
QueueScheduler::add_task(Connection* conn)
{
    utils::Lock lk(mutex_);
    NodeMap::iterator it = nodes_.find(conn->fd());
    if (it != nodes_.end()) {
        // already in the scheduler, put it on the top
        list_.erase(*it);
        list_.push_front(conn);
        nodes_.erase(conn->fd());
        nodes_.insert(conn->fd(), list_.begin());
        return;
    }
    bool need_notify = (list_.size() == 0);
    nodes_.insert(conn->fd(), list_.push_back(conn));
    lk.unlock();
    if (need_notify) {
        cond_.notify_all();
    }
}

void
QueueScheduler::reschedule()
{
    if (!suppress_connection_lock_) {
        cond_.notify_all();
    }
}

Connection*
QueueScheduler::pick_task_nolock_connection()
{
    utils::Lock lk(mutex_);
    while (list_.empty()) {
        if (!auto_wait(lk)) {
            return NULL;
        }
    }
    Connection* conn = list_.front();
    list_.pop_front();
    nodes_.erase(conn->fd());
    return conn;
}

Connection*
QueueScheduler::pick_task_lock_connection()
{
    utils::Lock lk(mutex_);
    while (list_.empty()) {
        if (!auto_wait(lk)) {
            return NULL;
        }
    }

reschedule:
    Connection* conn = NULL;
    for (NodeList::iterator it = list_.begin(); it != list_.end(); ++it) {
        conn = *it;
        if (conn->try_lock()) {
            list_.erase(it);
            nodes_.erase(conn->fd());
            return conn;
        }
    }
    if (!auto_wait(lk)) {
        return NULL;
    }
    goto reschedule;
}

Connection*
QueueScheduler::pick_task()
{
    if (suppress_connection_lock_) {
        return pick_task_nolock_connection();
    } else {
        return pick_task_lock_connection();
    }
}

void
QueueScheduler::remove_task(Connection* conn)
{
    utils::Lock lk(mutex_);
    NodeMap::iterator it = nodes_.find(conn->fd());
    if (it == nodes_.end()) {
        return;
    }
    list_.erase(*it);
    nodes_.erase(conn->fd());
}

QueueScheduler::~QueueScheduler()
{
    utils::Lock lk(mutex_);
    list_.clear();
}

Connection*
ConnectionFactory::create_connection(int fd)
{
    return new Connection(fd);
}

void
ConnectionFactory::destroy_connection(Connection* conn)
{
    delete conn;
}

Pipeline::Pipeline()
{
    factory_ = new ConnectionFactory();
}

Pipeline::~Pipeline()
{
    delete factory_;
}

void
Pipeline::set_connection_factory(ConnectionFactory* fac)
{
    delete factory_;
    factory_ = fac;
}

Stage*
Pipeline::find_stage(const std::string& name) const
{
    StageMap::const_iterator it = map_.find(name);
    if (it == map_.end()) {
        return NULL;
    } else {
        return it->second;
    }
}

void
Pipeline::initialize_stages()
{
    for (StageMap::iterator it = map_.begin(); it != map_.end(); ++it) {
        Stage* stage = it->second;
        stage->initialize();
    }
}

void
Pipeline::start_stages()
{
    for (StageMap::iterator it = map_.begin(); it != map_.end(); ++it) {
        Stage* stage = it->second;
        stage->start_thread_pool();
    }
}

Connection*
Pipeline::create_connection(int fd)
{
    return factory_->create_connection(fd);
}

void
Pipeline::dispose_connection(Connection* conn)
{
    LOG(DEBUG, "disposing connection %d %p", conn->fd(), conn);
    StageMap::iterator it = map_.begin();
    Stage* stage = NULL;

    conn->lock();
    while (it != map_.end()) {
        stage = it->second;
        if (stage) {
            stage->sched_remove(conn);
        }
        ++it;
    }
    ::close(conn->fd());
    conn->unlock();
    factory_->destroy_connection(conn);
    LOG(DEBUG, "disposed");
}

void
Pipeline::add_stage(const std::string& name, Stage* stage)
{
    if (name == "poll_in") {
        poll_in_stage_ = (PollInStage*) stage;
    } else if (name == "write_back") {
        write_back_stage_ = stage;
    } else if (name == "recycle") {
        recycle_stage_ = (RecycleStage*) stage;
    }
    map_.insert(std::make_pair(name, stage));
}

void
Pipeline::disable_poll(Connection* conn)
{
    poll_in_stage_->sched_remove(conn);
    utils::set_socket_blocking(conn->fd(), true);
}

void
Pipeline::enable_poll(Connection* conn)
{
    utils::set_socket_blocking(conn->fd(), false);
    if (conn->is_active()) {
        poll_in_stage_->sched_add(conn);
    }
}

void
Pipeline::reschedule_all()
{
    for (StageMap::iterator it = map_.begin(); it != map_.end(); ++it) {
        Stage* stage = it->second;
        stage->sched_reschedule();
    }
}

}

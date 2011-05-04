// -*- mode: c++ -*-

#ifndef _PIPELINE_H_
#define _PIPELINE_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <ctime>
#include <string>
#include <list>
#include <set>
#include <map>

#include "utils/fdmap.h"
#include "utils/misc.h"
#include "utils/list.h"
#include "core/stream.h"
#include "core/inet_address.h"
#include "core/timer.h"

namespace tube {

class Connection
{
public:
    union PollerSpecData {
        int   data_int;
        void* data_ptr;
    };

    Connection(int sock);
    virtual ~Connection() {}

    PollerSpecData poller_spec() const { return poller_spec_; }

    int fd() const { return fd_; }
    std::string address_string() const;
    bool is_cork_enabled() const { return cork_enabled_; }
    bool is_active() const { return !inactive_; }
    bool is_close_after_finish() const { return close_after_finish_; }

    void set_address(const InternetAddress& addr) { address_ = addr; }
    void set_idle_timeout(int sec) { timeout_ = sec; }
    void set_io_timeout(int msec);
    void set_cork_enabled(bool val) { cork_enabled_ = val; }
    void set_active(bool active) { inactive_ = !active; }
    void set_close_after_finish(bool val) { close_after_finish_ = val; }

    // timer related
    Timer::Unit last_active_time() const { return last_active_; }
    Timer::Unit timer_sched_time() const;
    bool        update_last_active();

    bool try_lock();
    void lock();
    void unlock();

    void set_cork();
    void clear_cork();

    const InputStream& in_stream() const { return in_stream_; }
    const OutputStream& out_stream() const { return out_stream_; }
    InputStream& in_stream() { return in_stream_; }
    OutputStream& out_stream() { return out_stream_; }

    void active_close();
protected:
    // poller specific data, might not be used
    PollerSpecData poller_spec_;

    int       fd_;
    int       timeout_;
    bool      cork_enabled_;
    bool      inactive_;

    InternetAddress address_;

    // input and output stream
    InputStream    in_stream_;
    OutputStream   out_stream_;

    // locks
    utils::Mutex mutex_;
    long         owner_;

    bool        close_after_finish_;
    Timer::Unit last_active_;
};

class Scheduler : utils::Noncopyable
{
public:
    Scheduler();
    virtual ~Scheduler();

    virtual void add_task(Connection* conn)     = 0;
    virtual Connection* pick_task()             = 0;
    virtual void remove_task(Connection* conn)  = 0;
    virtual void reschedule()                   = 0;
};

class QueueScheduler : public Scheduler
{
protected:
    typedef utils::MemoryPool<utils::NoThreadSafePool> MemoryPool;
    typedef utils::List<Connection*> NodeList;
    typedef utils::FDMap<NodeList::iterator> NodeMap;

    MemoryPool pool_;
    NodeList  list_;
    NodeMap   nodes_;

    utils::Mutex      mutex_;
    utils::Condition  cond_;

    bool      suppress_connection_lock_;
public:
    static size_t kMemoryPoolSize;

    QueueScheduler(bool suppress_connection_lock = false);
    ~QueueScheduler();

    virtual void        add_task(Connection* conn);
    virtual Connection* pick_task();
    virtual void        remove_task(Connection* conn);
    virtual void        reschedule();
private:
    Connection* pick_task_nolock_connection();
    Connection* pick_task_lock_connection();
};

class Stage;

struct ConnectionFactory
{
public:
    virtual Connection* create_connection(int fd);
    virtual void        destroy_connection(Connection* conn);
};

class PollInStage;

class Pipeline : utils::Noncopyable
{
    typedef std::map<std::string, Stage*> StageMap;
    StageMap       map_;
    utils::RWMutex mutex_;

    PollInStage*       poll_in_stage_;
    ConnectionFactory* factory_;

    Pipeline();
    ~Pipeline();

public:
    static Pipeline& instance() {
        static Pipeline ins;
        return ins;
    }

    utils::RWMutex& mutex() { return mutex_; }

    void add_stage(const std::string& name, Stage* stage);

    void set_connection_factory(ConnectionFactory* fac);

    PollInStage* poll_in_stage() const { return poll_in_stage_; }
    Stage* find_stage(const std::string& name);

    Connection* create_connection(int fd);
    void dispose_connection(Connection* conn);

    void disable_poll(Connection* conn);
    void enable_poll(Connection* conn);

    void reschedule_all();
};

}

#endif /* _PIPELINE_H_ */

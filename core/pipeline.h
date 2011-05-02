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

namespace tube {

struct Connection
{
    volatile uint32_t last_active;
    volatile uint32_t timeout;

    // poller specific data, might not be used
    union {
        int   data_int;
        void* data_ptr;
    } poller_spec;

    int       fd;
    int       prio;
    bool      cork_enabled;
    bool      inactive;

    InternetAddress address;

    // input and output stream
    InputStream    in_stream;
    OutputStream   out_stream;

    // locks
    utils::Mutex mutex;
    long         owner;

    bool close_after_finish;

    bool trylock();
    void lock();
    void unlock();

    std::string address_string() const;
    void set_timeout(int sec) { timeout = sec; }
    void set_io_timeout(int msec);
    void set_cork();
    void clear_cork();

    void active_close();

    Connection(int sock);
    virtual ~Connection() {}
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

class Timer
{
public:
    typedef time_t Unit;
    typedef void* Context;
    typedef boost::function<bool (Context)> Callback;

    static int kUnitGrand;

    bool set(Unit unit, Context ctx, Callback call);
    void replace(Unit unit, Context ctx, Callback call);
    bool remove(Unit unit, Context ctx, Callback& call);
    bool query(Unit unit, Context ctx, Callback& call);

    void process_callbacks();
    Unit current_timer_unit() const;

private:
    struct TimerKey {
        Unit unit;
        Context ctx;
        bool operator<(const TimerKey& rhs) const;
        TimerKey(Unit timerunit, Context context)
            : unit(timerunit), ctx(context) {}
    };
    typedef std::map<TimerKey, Callback> TimerTree;
    TimerTree rbtree_;

    bool invoke_callback(TimerTree::iterator it);
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

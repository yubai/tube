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
#include "utils/lock.h"
#include "utils/list.h"
#include "core/stream.h"
#include "core/inet_address.h"
#include "core/timer.h"
#include "core/controller.h"

namespace tube {

/**
 * Connection object encapsulate a client connection session.  It is used for
 * multiple purpose including polling, buffering input and output, TCP Cork
 * control, etc.
 *
 * Connection object hold per-connection lock used for scheduling.
 */
class Connection
{
public:
    /**
     * Poller specific data used for polling, primary used for solaris's port
     * completion framework
     */
    union PollerSpecData {
        int   data_int;
        void* data_ptr;
    };

    enum Flags {
        kFlagCorkEnabled      = 0x01,
        kFlagActive           = 0x02,
        kFlagCloseAfterFinish = 0x04,
        kFlagUrgent           = 0x08,
    };

    /**
     * @param sock The client socket.
     */
    Connection(int sock);
    virtual ~Connection() {}

    PollerSpecData poller_spec() const { return poller_spec_; }

    /**
     * @return The client socket file descriptor
     */
    int fd() const { return fd_; }
    /**
     * @return Client address in string format.
     */
    std::string address_string() const;
    /**
     * @return True if client enabled tcp cork control
     */
    bool is_cork_enabled() const { return (flags_ & kFlagCorkEnabled) != 0; }
    /**
     * @return True if client is not closed
     * @see set_cork(), clear_cok()
     */
    bool is_active() const { return (flags_ & kFlagActive) != 0; }
    /**
     * @return True if after tranfer complete, server is responsible to close
     * connection actively.
     */
    bool is_close_after_finish() const {
        return (flags_ & kFlagCloseAfterFinish) != 0;
    }
    /**
     * @return True if connection needs to be handled urgently.
     */
    bool is_urgent() const {
        return (flags_ & kFlagUrgent) != 0;
    }

    /**
     * Set an internet address.  Usually performed after a accept()
     * @param addr The internet address object
     */
    void set_address(const InternetAddress& addr) { address_ = addr; }
    /**
     * Connection will be closed after being idle for a long time.  Usually
     * for 10-30 seconds.
     * @param sec Maximum idle timeout
     */
    void set_idle_timeout(int sec) { timeout_ = sec; }
    /**
     * The maximum blocking time when doing blocking operating like write()
     * is being called.
     * @param msec Maximum blocking time in millisecond
     */
    void set_io_timeout(int msec);
    /**
     * @param val True if enable TCP Cork contorl, false if disabled.
     */
    void set_cork_enabled(bool val) {
        if (val) {
            flags_ |= kFlagCorkEnabled;
        } else {
            flags_ &= ~kFlagCorkEnabled;
        }
    }
    /**
     * Mark the connection active or inactive
     * @see set_cork(), clear_cork()
     */
    void set_active(bool active) {
        if (active) {
            flags_ |= kFlagActive;
        } else {
            flags_ &= ~kFlagActive;
        }
    }
    /**
     * @param val If true, server will be responsible to close the connection
     * actively after transfer complete.
     */
    void set_close_after_finish(bool val) {
        if (val) {
            flags_ |= kFlagCloseAfterFinish;
        } else {
            flags_ &= ~kFlagCloseAfterFinish;
        }
    }
    /**
     * @param val If true, make this connection as urgent.
     */
    void set_urgent(bool val) {
        if (val) {
            flags_ |= kFlagUrgent;
        } else {
            flags_ &= ~kFlagUrgent;
        }
    }

    // timer related
    /**
     * Timestamp for last active time.
     */
    Timer::Unit last_active_time() const { return last_active_; }
    /**
     * @return Timestamp for which timer routine (e.g. close the idle
     * connection) will be executed.
     */
    Timer::Unit timer_sched_time() const;
    /**
     * Update the last active time timestamp.
     * @return True if overwrite original timestamp, false means the original
     * timestamp is later or equal the current time.
     */
    bool        update_last_active();

    // lock related
    /**
     * Try lock the connection lock.
     * @return True means locking succeeded, false means cann't lock failed.
     */
    bool try_lock();
    /**
     * Lock the connection lock.
     */
    void lock();
    /**
     * Unlock the connection lock.
     */
    void unlock();

    /**
     * Set TCP Cork, all data that write() to client socket will be buffer until
     * exceeds a complete network frame.
     *
     * This is used for reducing the fragment packets and increasing the TCP
     * thoughputs.
     */
    void set_cork();
    /**
     * Clear TCP Cork and flush all data that previously has buffered by OS.
     * @see set_cork()
     */
    void clear_cork();

    /**
     * Get input stream of the connection object.
     */
    const InputStream& in_stream() const { return in_stream_; }
    /**
     * Get output stream of the connection object.
     */
    const OutputStream& out_stream() const { return out_stream_; }
    /**
     * Get input stream of the connection object.
     */
    InputStream& in_stream() { return in_stream_; }
    /**
     * Get output stream of the connection object.
     */
    OutputStream& out_stream() { return out_stream_; }

    /**
     * Actively close the connection.
     */
    void active_close();

    /// continuation support
    void  set_continuation(void* ptr) { continuation_data_ = ptr; }
    void* get_continuation() const { return continuation_data_; }
    void  reset_continuation() { continuation_data_ = NULL; }
    bool  has_continuation() const { return continuation_data_ != NULL; }

    virtual void resched_continuation() {};

protected:
    // poller specific data, might not be used
    PollerSpecData poller_spec_;

    int       fd_;
    int       timeout_;

    InternetAddress address_;

    // input and output stream
    InputStream    in_stream_;
    OutputStream   out_stream_;

    // locks
    utils::Mutex mutex_;
    long         owner_;

    int         flags_;
    Timer::Unit last_active_;

    void*       continuation_data_;
};

class Controller;

/**
 * Scheduler that schedule the client connection to stage processing routine.
 * This is the core part of stage/pipeline implementation.  The quality and
 * correctness of scheduling directly affects the quality and thoughputs.
 *
 * Connection are scheduled through pick_task() method.  pick_task() being
 * called by multiple threads in the thread pool.
 *
 * When no connection is active for schedule the scheduler will sleep, until a
 * call to reschedule(), will reschedule a connection is available.
 */
class Scheduler : utils::Noncopyable
{
protected:
    Controller* controller_;
public:
    Scheduler();
    virtual ~Scheduler();

    /**
     * Add a connection to the scheduler for scheduling.
     */
    virtual void add_task(Connection* conn)     = 0;
    /**
     * Pick a connection and remove it from schduler.
     */
    virtual Connection* pick_task()             = 0;
    /**
     * Remove a connection from the schduler.
     */
    virtual void remove_task(Connection* conn)  = 0;
    /**
     * Notify the scheduler to reschedule.
     */
    virtual void reschedule()                   = 0;
    /**
     * Number of task that this scheduler contains.
     *
     * This method is not thread-safe
     */
    virtual size_t size_nolock()                = 0;

    void set_controller(Controller* controller) { controller_ = controller; }
    Controller* controller() const { return controller_; }
};

/**
 * A scheduler implementation using link list.  Under optimisic circumstances (
 * where not connection are being locked by other stages or routine), it will
 * use constant time to pick a connection.  However, it will use O(n) time
 * under worst case.
 *
 * The QueueScheduler provides a non-locking option for not locking the
 * connection during pick_task() routine.  This option can be used for stages
 * such as BlockOutStage.
 */
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
    /**
     * Default memory pool size for link list
     */
    static size_t kMemoryPoolSize;

    /**
     * @param suppress_connection_lock Option to tell scheduler don't lock
     * the connection when pick_task().   This option can be used for stages
     * such as BlockOutStage.
     */
    QueueScheduler(bool suppress_connection_lock = false);
    ~QueueScheduler();

    virtual void        add_task(Connection* conn);
    /**
     * Pick a connection and remove it from scheduler.  When
     * suppress_connection_lock is set this routine will not lock the
     * conenction, and this can be used in stages such as BlockOutStage
     * @return The connection. NULL means nothing to schedule and thread should
     * exit therefore.
     */
    virtual Connection* pick_task();
    virtual void        remove_task(Connection* conn);
    virtual void        reschedule();
    virtual size_t      size_nolock() { return list_.size(); }
private:
    Connection* pick_task_nolock_connection();
    Connection* pick_task_lock_connection();

    bool auto_wait(utils::Lock& lk);
};

class Stage;
class PollInStage;
class RecycleStage;

/**
 * Connection factory that used for creating connection objects.  This is
 * overwroted when server want to create its own connection object, such as
 * HttpConnection.
 *
 * Also note that this gives an opportunity for ConnectionFactory to have a
 * memory pool preallocate the connection object, which will increase the
 * accept() performance.
 */
class ConnectionFactory
{
public:
    virtual Connection* create_connection(int fd);
    virtual void        destroy_connection(Connection* conn);
};

/**
 * Global pipeline control object.
 *
 * It is designed as a singleton object that create, controls, configure the
 * connection objects and stages.  It has as read-write lock which can be used
 * for implementing resource recycle.
 */
class Pipeline : utils::Noncopyable
{
    typedef std::map<std::string, Stage*> StageMap;
    StageMap       map_;

    PollInStage*       poll_in_stage_;
    Stage*             write_back_stage_;
    RecycleStage*      recycle_stage_;
    ConnectionFactory* factory_;

    Pipeline();
    ~Pipeline();

public:
    /**
     * Get the singleton instance.
     */
    static Pipeline& instance() {
        static Pipeline ins;
        return ins;
    }

    /**
     * Register the custom ConnectionFactory.
     */
    void set_connection_factory(ConnectionFactory* fac);

    PollInStage*    poll_in_stage() const { return poll_in_stage_; }
    Stage*          write_back_stage() const { return write_back_stage_; }
    RecycleStage*   recycle_stage() const { return recycle_stage_; }

    /**
     * Register a stage.  This routine is automaticall called when Stage
     * is contructed.
     * @param name The name of the stage.
     * @param stage Stage object.
     */
    void            add_stage(const std::string& name, Stage* stage);
    /**
     * Find a stage according to its name.
     * @param name The name of the stage.
     * @return Stage object pointer if found, otherwise, NULL is returned.
     */
    Stage*          find_stage(const std::string& name) const;
    /**
     * Initialize all stages registered on pipeline.  Should be called when
     * server initialization.
     */
    void            initialize_stages();
    /**
     * Start every thread in thread pool of each stage.  Should be called when
     * server initialization.
     */
    void            start_stages();

    /**
     * Create the connection.
     * @param fd Client socket file descriptor.
     */
    Connection* create_connection(int fd);
    /**
     * Dispose the connection and recycle the resouces.
     */
    void        dispose_connection(Connection* conn);

    /**
     * Disable IO poll for a specific connection on PollInStage.
     */
    void disable_poll(Connection* conn);
    /**
     * Enable IO poll for a specific connection on PollInStage.
     */
    void enable_poll(Connection* conn);

    /**
     * Notify every stage to reschedule.  This method is called sometimes when
     * some connection is unlocked, so that the starving scheduler is able to
     * continue scheduling.
     */
    void reschedule_all();
};

}

#endif /* _PIPELINE_H_ */

// -*- mode: c++ -*-

#ifndef _STAGES_H_
#define _STAGES_H_

#include <queue>
#include <vector>
#include <set>

#include "core/pipeline.h"
#include "core/poller.h"
#include "core/controller.h"
#include "utils/misc.h"
#include "utils/lock.h"

namespace tube {

/**
 * Stage is a primary task processing module in Tube.  It consists of a
 * scheduler and a thread pool.  Each thread in thread pool will acquire a
 * connection using the scheduler and process it.
 *
 * Some stage (like the PollInStage) will not use the scheduler as defined by
 * Scheduler interface.  Instead they rely on OS's IO polling mechanism such as
 * epoll() or kqueue().
 */
class Stage
{
protected:
    Scheduler* sched_;
    Pipeline&  pipeline_;
    size_t     thread_pool_size_;
protected:
    virtual int process_task(Connection* conn) { return 0; };
public:
    static const int kStageReleaseLock = 0;
    static const int kStageKeepLock = -1;
    /**
     * Constructor for Stage object
     * @param name Name of the Stage object.
     */
    Stage(const std::string& name);
    virtual ~Stage() {}

    /**
     * Initialize the stage internal data structure.  Most implementation will
     * perform data look up in this routine.
     */
    virtual void initialize() {}

    /**
     * Add the connection to stage's internal scheduler.
     * @param conn Connection object to be added.
     */
    virtual bool sched_add(Connection* conn);
    /**
     * Remove the connection from stage's interan scheduler.
     * @param conn Connection object to be removed.
     */
    virtual void sched_remove(Connection* conn);
    /**
     * Notify the internal scheduler to reschedule.
     */
    virtual void sched_reschedule();

    /**
     * Main loop of thread routine.
     */
    virtual void main_loop();

    size_t     thread_pool_size() const { return thread_pool_size_; }
    Scheduler* scheduler() const { return sched_; }
    void       set_thread_pool_size(size_t size) { thread_pool_size_ = size; }

    /**
     * Start a single thread
     */
    virtual utils::ThreadId start_thread();
    /**
     * Start all threads in the thread pool.
     */
    virtual void start_thread_pool();
};

class PollStage : public Stage
{
protected:
    static int kDefaultTimeout;

    PollStage(const std::string& name);
    virtual ~PollStage() {}

    void add_poll(Poller* poller);
    void trigger_timer_callback(Poller& poller);
    void update_connection(Poller& poller, Connection* conn,
                           Timer::Callback cb);
public:
    int timeout() const { return timeout_; }
    /**
     * Set maximum blocking time for IO poller, this is also the time grand
     * for detecting idle connections.
     * @return Timeout in seconds.
     */
    void set_timeout(int timeout) { timeout_ = timeout; }

protected:
    utils::Mutex         mutex_;
    std::vector<Poller*> pollers_;
    int                  timeout_;
    size_t               current_poller_;
    std::string          poller_name_;
};

/**
 * IO poller stage implementation.  It abstract the IO poller into stage,
 * therefore rely on IO poller scheduling rather than external scheduler.
 *
 * IO poller on most OS have a timeout parameter, which specify the longest
 * blocking time for a poll() call.  In PollInStage, it can be tuned by calling
 * set_timeout() method.
 */
class PollInStage : public PollStage
{
    Stage* parser_stage_;
    Stage* recycle_stage_;
public:
    PollInStage();
    ~PollInStage();

    virtual bool sched_add(Connection* conn);
    virtual void sched_remove(Connection* conn);

    virtual void initialize();
    virtual void main_loop();

    /**
     * Clean up unused connection.  It remove its file descriptor from IO poller
     * and schedule it on the recycle stage for resource collection.
     */
    void cleanup_connection(Connection* conn);
private:
    void cleanup_connection(Poller& poller, Connection* conn);
    void read_connection(Poller& poller, Connection* conn);
    bool cleanup_idle_connection_callback(Poller& poller, void* ptr);
    void handle_connection(Poller& poller, Connection* conn, PollerEvent evt);
    void post_handle_connection(Poller& poller);
};

/**
 * BlockOutStage is responsible for transfer the Writeable in the
 * connection back to client.  Since from the time connection is scheduled on
 * this stage it's already locked, so the scheduler is will not lock the
 * connection on pick_task().
 */
class BlockOutStage : public Stage
{
public:
    BlockOutStage();
    virtual ~BlockOutStage();

    virtual bool sched_add(Connection* conn);
    virtual int  process_task(Connection* conn);
};

/**
 * PollOutStage is an alternative to BlockOutStage by using IO polling.  Compare
 * to BlockOutStage, it's has very subtle system call overhead, but fair on slow
 * connections.
 */
class PollOutStage : public PollStage
{
public:
    PollOutStage();
    virtual ~PollOutStage();

    virtual bool sched_add(Connection* conn);
    virtual void main_loop();
private:
    void cleanup_connection(Poller& poller, Connection* conn);
    void handle_connection(Poller& poller, Connection* conn, PollerEvent evt);
    void post_handle_connection(Poller& poller);
    bool cleanup_idle_connection_callback(Poller& poller, void* ptr);
};

/**
 * ParserStage is used to process buffer read from client.  Server wish to
 * write a protocol parser should inheritance this class.
 */
class ParserStage : public Stage
{
public:
    ParserStage();
    virtual ~ParserStage();
};

/**
 * RecycleStage is responsible for collection unused connection objects.  When
 * a conection is no longer used, it will be add on to this stage.
 *
 * RecycleStage use a simple queue for scheduling, so no external scheduler are
 * used.  Yet, cannot tolerent any duplicate add.  This is the same consequence
 * of double free.
 *
 * RecycleStage have a threshold for collecting, because collecting routine
 * will affect scale of the whole server.
 */
class RecycleStage : public Stage
{
    utils::Mutex            mutex_;
    utils::Condition        cond_;
    std::queue<Connection*> queue_;
    size_t                  recycle_batch_size_;
public:
    RecycleStage();
    virtual ~RecycleStage() {}

    virtual bool sched_add(Connection* conn);
    virtual void sched_remove(Connection* conn) {}

    virtual void main_loop();
    virtual void start_thread_pool();

    /**
     * Set recycle threshold.  Recycle routine will not start collecting until
     * number of unused connection exceeds this number.
     */
    void set_recycle_batch_size(size_t size) { recycle_batch_size_ = size; }
};

}

#endif /* _STAGES_H_ */

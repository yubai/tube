// -*- mode: c++ -*-

#ifndef _STAGES_H_
#define _STAGES_H_

#include <queue>
#include <vector>
#include <set>

#include "core/pipeline.h"
#include "core/poller.h"

namespace tube {

class Stage
{
protected:
    Scheduler* sched_;
    Pipeline&  pipeline_;
    size_t     thread_pool_size_;
protected:
    Stage(const std::string& name);
    virtual ~Stage() {}

    virtual int process_task(Connection* conn) { return 0; };
public:
    virtual void initialize() {}

    virtual bool sched_add(Connection* conn);
    virtual void sched_remove(Connection* conn);
    virtual void sched_reschedule();

    virtual void main_loop();

    size_t     thread_pool_size() const { return thread_pool_size_; }
    Scheduler* scheduler() const { return sched_; }
    void       set_thread_pool_size(size_t size) { thread_pool_size_ = size; }

    virtual void start_thread();
    virtual void start_thread_pool();
};

class PollInStage : public Stage
{
    utils::Mutex      mutex_;

    std::vector<Poller*> pollers_;
    size_t               current_poller_;
    std::string          poller_name_;
    int                  timeout_;

    Stage* parser_stage_;
    Stage* recycle_stage_;
public:
    static int kDefaultTimeout;

    PollInStage();
    ~PollInStage();

    int timeout() const { return timeout_; }
    void set_timeout(int timeout) { timeout_ = timeout; }

    virtual bool sched_add(Connection* conn);
    virtual void sched_remove(Connection* conn);

    virtual void initialize();
    virtual void main_loop();

    void cleanup_connection(Connection* conn);
private:
    void cleanup_connection(Poller& poller, Connection* conn);
    void read_connection(Poller& poller, Connection* conn);
    bool cleanup_idle_connection_callback(Poller& poller, void* ptr);
    void add_poll(Poller* poller);
    void handle_connection(Poller& poller, Connection* conn, PollerEvent evt);
    void post_handle_connection(Poller& poller);
};

class WriteBackStage : public Stage
{
public:
    WriteBackStage();
    virtual ~WriteBackStage();

    virtual bool sched_add(Connection* conn);
    virtual int  process_task(Connection* conn);
};

class ParserStage : public Stage
{
public:
    ParserStage();
    virtual ~ParserStage();
};

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

    void set_recycle_batch_size(size_t size) { recycle_batch_size_ = size; }
};

}

#endif /* _STAGES_H_ */

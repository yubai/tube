// -*- mode: c++ -*-

#ifndef _STAGES_H_
#define _STAGES_H_

#include <sys/epoll.h>
#include <queue>
#include <vector>
#include <set>

#include "core/pipeline.h"

namespace pipeserv {

class Stage
{
protected:
    Scheduler* sched_;
protected:
    Stage(std::string name);
    virtual ~Stage() {}

    virtual int process_task(Connection* conn) {};
public:
    virtual void initialize() {}

    virtual bool sched_add(Connection* conn);
    virtual void sched_remove(Connection* conn);

    virtual void main_loop();

    void start_thread();
};

class PollInStage : public Stage
{
    typedef std::set<int> PollSet;
    struct Poll {
        PollSet fds;
        int     poll_fd;

        Poll(int fd) : poll_fd(fd) {}
    };

    utils::Mutex      mutex_;
    std::vector<Poll> polls_;

    int max_event_;
    int timeout_;

    Stage* parser_stage_;
    Stage* recycle_stage_;
public:
    PollInStage(int max_event = 256);
    ~PollInStage();

    int timeout() const { return timeout_; }

    void set_timeout(int timeout) { timeout_ = timeout; }

    virtual bool sched_add(Connection* conn);
    virtual void sched_remove(Connection* conn);

    virtual void initialize();
    virtual void main_loop();
private:
    void add_poll(int poll_fd);
    void read_connection(Connection* conn);
    void cleanup_connection(Connection* conn);
};


class WriteBackStage : public Stage
{
public:
    WriteBackStage();
    virtual ~WriteBackStage();

    virtual int process_task(Connection* conn);
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
    RecycleStage(size_t recycle_batch_size = 1)
        : Stage("recycle"), recycle_batch_size_(recycle_batch_size) {}

    virtual ~RecycleStage() {}

    virtual bool sched_add(Connection* conn);
    virtual void sched_remove(Connection* conn) {}

    virtual void main_loop();
};

}

#endif /* _STAGES_H_ */

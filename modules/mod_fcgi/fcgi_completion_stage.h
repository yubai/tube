// -*- mode: c++ -*-
#ifndef _FCGI_COMPLETION_STAGE_H_
#define _FCGI_COMPLETION_STAGE_H_

#include <string>

#include "core/stages.h"
#include "core/pipeline.h"
#include "http/connection.h"

#include "fcgi_proto.h"

namespace tube {
namespace fcgi {

enum FcgiCompletionStatus
{
    kCompletionReadClient, // read from client buffer
    kCompletionWriteFcgi, // write the read buffer into fcgi server
    kCompletionReadFcgi, // read from fcgi server
    kCompletionHeadersDone, // all headers are parsed by content parser
    kCompletionContinue, // triggered write back using PollOutStage
    kCompletionEOF, // triggered write back using PollOutStage, but mark as EOF
    kCompletionError, // error with fcgi server
    kCompletionTimeout, // fcgi server timed out
};

struct FcgiCompletionContinuation
{
    int                  sock_fd;
    bool                 need_reconnect;
    FcgiCompletionStatus status;
    Buffer               task_buffer;
    int                  task_len;
    FcgiResponseParser   response_parser;
    FcgiContentParser    content_parser;
    Buffer               output_buffer;
    Timer::Unit          last_active;

    FcgiCompletionContinuation()
        : sock_fd(0), need_reconnect(false), status(kCompletionReadClient),
          task_len(0), response_parser(task_buffer), last_active(0) {}

    static const size_t  kTaskBufferLimit;

    bool        update_last_active();
};

class FcgiCompletionStage : public PollStage
{
    Stage*            write_back_stage_;
public:
    static Timer::Unit kIdleTimeout;

    FcgiCompletionStage();
    virtual ~FcgiCompletionStage();

    void initialize();

    bool sched_add(Connection* conn);
    void sched_remove(Connection* conn);

    void main_loop();

    void stream_write_back(HttpConnection* conn);
    bool run_parser(FcgiCompletionContinuation* cont);

private:
    void handle_connection(Poller& poller, Connection* conn, PollerEvent evt);
    void post_handle_connection(Poller& poller);

    void abort(Poller& poller, Connection* conn, FcgiCompletionStatus status,
               bool remove_timer = true);
    bool remove_fd_with_timer(Poller& poll, int fd, Connection* conn);

    void handle_read(Poller& poller, Connection* conn);
    void handle_write(Poller& poller, Connection* conn);
    void handle_error(Poller& poller, Connection* conn);

    FcgiCompletionStatus transfer_status(Poller& poller, HttpConnection* conn);

    // clean up routines
    void update_idle_handler(Poller& poller, FcgiCompletionContinuation* cont,
                             Connection* conn);
    bool cleanup_idle_callback(Poller& poller, void* ptr);
};

}
}

#endif /* _FCGI_COMPLETION_STAGE_H_ */

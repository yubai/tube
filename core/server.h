// -*- mode: c++ -*-

#ifndef _SERVER_H_
#define _SERVER_H_

#include <sys/types.h>
#include <sys/socket.h>

#include "core/stages.h"

namespace tube {

/**
 * Server object that bind and listen on an address.  It's responsible to
 * accept new connection and add it into PollInStage, starting the connection's
 * normal lifecycle.
 */
class Server
{
    int fd_;
    size_t addr_size_;

    PollInStage*       poll_in_stage_;
    RecycleStage*      recycle_stage_;
    Stage*             write_back_stage_;
public:
    enum WriteBackMode {
        kWriteBackModeBlock,
        kWriteBackModePoll
    };

    static WriteBackMode kDefaultWriteBackMode;
    Server();
    virtual ~Server();

    /**
     * @return File descriptor of server socket.
     */
    int fd() const { return fd_; }

    /**
     * Bind on the address host and port service.  Since port can be specified
     * in string format under unix system (the service), service paramter is
     * represent as a char*.
     * @param host The address.
     * @param service Service or port. In string format.
     */
    void bind(const char* host, const char* service);
    /**
     * Listen the server socket.
     * @param queue_size Queue size for listen() system call.
     */
    void listen(int queue_size);
    /**
     * Start the main loop.  The main loop keeps accept new connection and
     * start the connection's normal lifecycle.
     */
    void main_loop();

    // simple wrappers to Pipeline
    /**
     * Initialize all the stages.  This is a simple wrapper for Pipeline object.
     */
    void initialize_stages();
    /**
     * Start all threads in thread pool of each stage.  This is a simple wrapper
     * for Pipeline object.
     */
    void start_stages();
};

}

#endif /* _SERVER_H_ */

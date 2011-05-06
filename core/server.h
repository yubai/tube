// -*- mode: c++ -*-

#ifndef _SERVER_H_
#define _SERVER_H_

#include <sys/types.h>
#include <sys/socket.h>

#include "core/stages.h"

namespace tube {

class Server
{
    int fd_;
    size_t addr_size_;

    PollInStage*       poll_in_stage_;
    WriteBackStage*    write_back_stage_;
    RecycleStage*      recycle_stage_;
public:
    Server();
    virtual ~Server();

    int fd() const { return fd_; }

    void bind(const char* host, const char* service);
    void listen(int queue_size);
    void main_loop();

    // simple wrappers to Pipeline
    void initialize_stages();
    void start_stages();
};

}

#endif /* _SERVER_H_ */

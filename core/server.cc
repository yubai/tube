#include "pch.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <cstdlib>
#include <signal.h>

#include "core/server.h"
#include "core/pipeline.h"
#include "core/stages.h"

#include "utils/exception.h"
#include "utils/logger.h"
#include "utils/misc.h"

namespace tube {

static struct addrinfo*
lookup_addr(const char* host, const char* service)
{
    struct addrinfo hints;
    struct addrinfo* info;

    memset(&hints, 0, sizeof(addrinfo));
    hints.ai_family = AF_UNSPEC; // allow both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;
    if (::getaddrinfo(host, service, &hints, &info) < 0) {
        throw utils::SyscallException();
    }
    return info;
}

Server::WriteBackMode
Server::kDefaultWriteBackMode = Server::kWriteBackModePoll;

Server::Server()
    : fd_(-1), addr_size_(0), write_back_stage_(NULL)
{
    // construct all essential stages
    poll_in_stage_ = new PollInStage();
    if (kDefaultWriteBackMode == kWriteBackModeBlock) {
        write_back_stage_ = new BlockOutStage();
    } else if (kDefaultWriteBackMode == kWriteBackModePoll) {
        write_back_stage_ = new PollOutStage();
    }
}

Server::~Server()
{
    delete poll_in_stage_;
    delete write_back_stage_;

    if (fd_ > 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
    }
}

void
Server::initialize_stages()
{
    Pipeline::instance().initialize_stages();
}

void
Server::start_stages()
{
    Pipeline::instance().start_stages();
}

void
Server::bind(const char* host, const char* service)
{
    struct addrinfo* info = lookup_addr(host, service);
    bool done = false;
    for (struct addrinfo* p = info; p != NULL; p = p->ai_next) {
        if ((fd_ = ::socket(p->ai_family, p->ai_socktype, 0)) < 0) {
            continue;
        }
        if (::bind(fd_, p->ai_addr, p->ai_addrlen) < 0) {
            close(fd_);
            continue;
        }
        done = true;
        addr_size_ = p->ai_addrlen;
        break;
    }
    ::freeaddrinfo(info);
    if (!done) {
        std::string err = "Cannot bind port(service) ";
        err += service;
        err += " on host ";
        err += host;
        err += " error code: ";
        err += strerror(errno);
        throw std::invalid_argument(err);
    }
}

void
Server::listen(int queue_size)
{
    if (::listen(fd_, queue_size) < 0)
        throw utils::SyscallException();
}

void
Server::main_loop()
{
    Pipeline& pipeline = Pipeline::instance();
    Stage* stage = pipeline.find_stage("poll_in");
    while (true) {
        InternetAddress address;
        socklen_t socklen = address.max_address_length();
        int client_fd = ::accept(fd_, address.get_address(), &socklen);
        if (client_fd < 0) {
            LOG(WARNING, "Error when accepting socket: %s", strerror(errno));
            continue;
        }
        // set non-blocking mode
        Connection* conn = pipeline.create_connection(client_fd);
        conn->set_address(address);
        utils::set_socket_blocking(conn->fd(), false);

        LOG(DEBUG, "accepted connection from %s",
            conn->address_string().c_str());
        stage->sched_add(conn);
    }
}

}

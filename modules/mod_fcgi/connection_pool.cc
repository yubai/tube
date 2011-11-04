#include <errno.h>
#include <netdb.h>
#include <limits.h>

#include "connection_pool.h"
#include "utils/logger.h"

namespace tube {
namespace fcgi {

ConnectionPool::ConnectionPool(const std::string& address, int max_n_sockets)
    : address_(address), max_n_sockets_(max_n_sockets)
{
    if (max_n_sockets_ > 0) {
        sockets_.reserve(max_n_sockets_);
    }
}

ConnectionPool::~ConnectionPool()
{
    for (size_t i = 0; i < sockets_.size(); i++) {
        ::shutdown(sockets_[i], SHUT_RDWR);
        ::close(sockets_[i]);
    }
}

int
ConnectionPool::alloc_connection()
{
    int sock = -1;
    utils::Lock lk(mutex_);
    if (free_connections_.empty()) {
        if (max_n_sockets_ > 0
               && sockets_.size() > (size_t) max_n_sockets_) {
            // number of sockets limit exceeds, wait
            cond_.wait(lk);
            goto done;
        }
        sock = create_socket();
        if (sock > 0 && connect(sock)) {
            sockets_.push_back(sock);
        }
        return sock;
    }
done:
    sock = free_connections_.back();
    free_connections_.pop_back();
    return sock;
}

void
ConnectionPool::reclaim_connection(int sock)
{
    utils::Lock lk(mutex_);
    free_connections_.push_back(sock);
    cond_.notify_one();
}

UnixConnectionPool::UnixConnectionPool(const std::string& address,
                                       int max_n_sockets)
    : ConnectionPool(address, max_n_sockets)
{
    memset(&sock_addr_, 0, sizeof(struct sockaddr_un));
    sock_addr_.sun_family = AF_UNIX;
    memcpy(sock_addr_.sun_path, address.c_str(),
           sizeof(sock_addr_.sun_path) - 1);
}

int
UnixConnectionPool::create_socket()
{
    int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG(ERROR, "cannot create unix domain socket: %s", strerror(errno));
    }
    return sock;
}

bool
UnixConnectionPool::connect(int sock)
{
    if (::connect(sock, (struct sockaddr*) &sock_addr_,
                  sizeof(struct sockaddr_un)) < 0) {
        LOG(ERROR, "cannot connect to unix domain socket: %s", strerror(errno));
        return false;
    }
    return true;
}

TcpConnectionPool::TcpConnectionPool(const std::string& address,
                                     int max_n_sockets)
    : ConnectionPool(address, max_n_sockets), sock_addr_(NULL),
      sock_addr_len_(0)
{
    // parse the address
    size_t pos = address.find(':');
    std::string host = address.substr(0, pos);
    std::string port = address.substr(pos + 1);
    // lookup the address. because it could be neither IPv4 or IPv6
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0
        || result == NULL) {
        LOG(ERROR, "cannot lookup address %s", host.c_str());
        return;
    }
    sock_addr_ = (struct sockaddr*) malloc(result->ai_addrlen);
    sock_addr_len_ = result->ai_addrlen;
    memcpy(sock_addr_, result->ai_addr, sock_addr_len_);
    sock_family_ = result->ai_family;
    sock_type_ = result->ai_socktype;
    sock_proto_ = result->ai_protocol;
}

TcpConnectionPool::~TcpConnectionPool()
{
    free(sock_addr_);
}

int
TcpConnectionPool::create_socket()
{
    if (sock_addr_ == NULL) {
        return -1; // error
    }
    int sock = socket(sock_family_, sock_type_, sock_proto_);
    if (sock < 0) {
        LOG(ERROR, "cannot create socket: %s", strerror(errno));
    }
    return sock;
}

bool
TcpConnectionPool::connect(int sock)
{
    if (::connect(sock, sock_addr_, sock_addr_len_) < 0) {
        LOG(ERROR, "cannot connect socket: %s", strerror(errno));
        return false;
    }
    return true;
}

}
}

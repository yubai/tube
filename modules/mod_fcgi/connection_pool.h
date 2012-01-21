// -*- mode: c++ -*-

#ifndef _CONNECTIONPOOL_H_
#define _CONNECTIONPOOL_H_

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string>
#include <vector>
#include "utils/misc.h"
#include "utils/lock.h"

namespace tube {
namespace fcgi {

class ConnectionPool
{
public:
    ConnectionPool(const std::string& address, int max_n_sockets);
    virtual ~ConnectionPool();

    int  alloc_connection(bool& is_connected);
    void reclaim_connection(int sock);
    void reclaim_inactive_connection(int sock);

    virtual bool connect(int sock) = 0;
protected:
    virtual int  create_socket() = 0;

protected:
    std::string address_;
    int max_n_sockets_;
    bool initialized_;

    std::vector<int> active_connections_;
    std::vector<int> inactive_connections_;
    utils::Mutex     mutex_;
    utils::Condition cond_;
};

class UnixConnectionPool : public ConnectionPool
{
public:
    UnixConnectionPool(const std::string& address, int max_n_sockets);
    virtual ~UnixConnectionPool() {}

protected:
    virtual int  create_socket();
    virtual bool connect(int sock);
private:
    struct sockaddr_un sock_addr_;
};

class TcpConnectionPool : public ConnectionPool
{
public:
    TcpConnectionPool(const std::string& address, int max_n_sockets);
    virtual ~TcpConnectionPool();

protected:
    virtual int  create_socket();
    virtual bool connect(int sock);
private:
    struct sockaddr* sock_addr_;
    socklen_t        sock_addr_len_;

    int sock_family_, sock_type_, sock_proto_;
};

}
}

#endif /* _CONNECTIONPOOL_H_ */

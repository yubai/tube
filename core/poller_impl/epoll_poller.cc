#include "pch.h"

#include "config.h"

#ifndef USE_EPOLL
#error "epoll is not supported"
#endif

#include <errno.h>
#include <limits.h>
#include <sys/epoll.h>

#include "utils/exception.h"
#include "utils/logger.h"
#include "core/poller.h"

namespace tube {

class EpollPoller : public Poller
{
    int epoll_fd_;
public:
    EpollPoller() ;
    virtual ~EpollPoller();

    virtual void handle_event(int timeout) ;
    virtual bool poll_add_fd(int fd, Connection* conn, PollerEvent evt);
    virtual bool poll_change_fd(int fd, Connection* conn, PollerEvent evt);
    virtual bool poll_remove_fd(int fd);
private:
    bool poll_add_or_change(int fd, Connection* con, PollerEvent evt,
                            bool change);
};

EpollPoller::EpollPoller()
    : Poller()
{
    epoll_fd_ = ::epoll_create(INT_MAX); // should ignore
    if (epoll_fd_ < 0) {
        throw utils::SyscallException();
    }
}

EpollPoller::~EpollPoller()
{
    ::close(epoll_fd_);
}

static int
build_epoll_event(PollerEvent evt)
{
    int res = 0;
    if (evt & kPollerEventRead) res |= EPOLLIN;
    if (evt & kPollerEventWrite) res |= EPOLLOUT;
    if (evt & kPollerEventError) res |= EPOLLERR;
    if (evt & kPollerEventHup) res |= EPOLLHUP;
    return res;
}

static PollerEvent
build_poller_event(int events)
{
    PollerEvent evt = 0;
    if (events & EPOLLIN) evt |= kPollerEventRead;
    if (events & EPOLLOUT) evt |= kPollerEventWrite;
    if (events & EPOLLERR) evt |= kPollerEventError;
    if (events & EPOLLHUP) evt |= kPollerEventHup;
    return evt;
}

bool
EpollPoller::poll_add_or_change(int fd, Connection* conn, PollerEvent evt,
                                bool change)
{
    struct epoll_event epoll_evt;
    memset(&epoll_evt, 0, sizeof(struct epoll_event));
    epoll_evt.events = build_epoll_event(evt);
    epoll_evt.data.ptr = conn;
    int flag = EPOLL_CTL_ADD;
    if (change) flag = EPOLL_CTL_MOD;
    if (epoll_ctl(epoll_fd_, flag, fd, &epoll_evt) < 0) {
        if (errno != EEXIST) {
            // fd is not watched
            LOG(WARNING, "add to epoll failed, remove fd %d", fd);
            return false;
        }
    }
    return true;
}

bool
EpollPoller::poll_add_fd(int fd, Connection* conn, PollerEvent evt)
{
    return poll_add_or_change(fd, conn, evt, false);
}

bool
EpollPoller::poll_change_fd(int fd, Connection* conn, PollerEvent evt)
{
    return poll_add_or_change(fd, conn, evt, true);
}

bool
EpollPoller::poll_remove_fd(int fd)
{
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, NULL) < 0) {
        if (errno != ENOENT) {
            // fd is watched
            LOG(WARNING, "remove from epoll failed, re-add the fd %d", fd);
            return false;
        }
    }
    return true;
}

#define MAX_EVENT_PER_POLL 4096

void
EpollPoller::handle_event(int timeout)
{
    struct epoll_event* epoll_evt = (struct epoll_event*)
        malloc(sizeof(struct epoll_event) * MAX_EVENT_PER_POLL);
    Connection* conn = NULL;
    if (timeout > 0) {
        timeout *= 1000;
    }
    while (true) {
        int nfds = epoll_wait(epoll_fd_, epoll_evt, MAX_EVENT_PER_POLL,
                              timeout);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                free(epoll_evt);
                throw utils::SyscallException();
            }
        }
        if (!pre_handler_.empty())
            pre_handler_();
        if (!handler_.empty()) {
            for (int i = 0; i < nfds; i++) {
                conn = (Connection*) epoll_evt[i].data.ptr;
                handler_(conn, build_poller_event(epoll_evt[i].events));
            }
        }
        if (!post_handler_.empty())
            post_handler_();
    }
    free(epoll_evt);
}

EXPORT_POLLER_IMPL(epoll, EpollPoller);

}

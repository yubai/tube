#include "pch.h"

#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "utils/misc.h"
#include "utils/exception.h"

namespace tube {
namespace utils {

void
set_socket_blocking(int fd, bool block)
{
    int flags = -1, origflags = -1;
    origflags = flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        throw SyscallException();
    if (block) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }
    if (origflags != flags && fcntl(fd, F_SETFL, flags) < 0)
        throw SyscallException();
}

void
set_fdtable_size(size_t size)
{
    struct rlimit lmt;
    lmt.rlim_cur = size;
    lmt.rlim_max = size;

    setrlimit(RLIMIT_NOFILE, &lmt);
}

bool
ignore_compare(const std::string& p, const std::string& q)
{
    if (p.length() != q.length())
        return false;
    for (size_t i = 0; i < p.length(); i++) {
        if (p[i] != q[i] && abs(p[i] - q[i]) != 'a' - 'A')
            return false;
    }
    return true;
}

std::string
string_to_upper_case(const std::string& str)
{
    std::string result = str;
    for (size_t i = 0; i < str.length(); i++) {
        result[i] = toupper(result[i]);
    }
    return result;
}

bool
parse_bool(const std::string& str)
{
    if (ignore_compare(str, "on") || ignore_compare(str, "true")) {
        return true;
    } else {
        return false;
    }
}

int
parse_int(const std::string& str)
{
    return atoi(str.c_str());
}

void
block_sigpipe()
{
    sigset_t bset;
    sigemptyset(&bset);
    sigaddset(&bset, SIGPIPE);
    sigprocmask(SIG_BLOCK, &bset, NULL);
}

static void*
thr_routine(void* data)
{
    boost::function<void ()>* ptr = (boost::function<void ()>*) data;
    (*ptr)();
    delete ptr;
    return NULL;
}

ThreadId
create_thread(boost::function<void ()> func)
{
    boost::function<void ()>* heap_func = new boost::function<void ()>(func);
    pthread_t tid = 0;
    if (pthread_create(&tid, NULL, &thr_routine, heap_func) != 0) {
        return -1;
    }
    return tid;
}

ThreadId
thread_id()
{
    return pthread_self();
}

}
}

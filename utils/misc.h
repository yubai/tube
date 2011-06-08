// -*- mode: c++ -*-

#ifndef _MISC_H_
#define _MISC_H_

#include <sys/types.h>
#include <fcntl.h>

// Must disable assert, because pthread_mutex_unlock on BSD will return an error
// when mutex is locked by a different thread.
#define BOOST_DISABLE_ASSERTS

#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread.hpp>
#include <boost/noncopyable.hpp>

namespace tube {

// only for LP/LLP64 model
typedef unsigned char byte;

typedef unsigned char          u8;
typedef unsigned short int     u16;
typedef unsigned int           u32;
typedef unsigned long long int u64;

typedef char          int8;
typedef short int     int16;
typedef int           int32;
typedef long long int int64;

namespace utils {

typedef boost::mutex Mutex;
typedef boost::unique_lock<Mutex> Lock;
typedef boost::condition Condition;

typedef boost::shared_mutex RWMutex;
typedef boost::shared_lock<RWMutex> SLock;
typedef boost::unique_lock<RWMutex> XLock;

typedef boost::thread Thread;
typedef boost::thread::id ThreadId;

typedef boost::noncopyable Noncopyable;
typedef boost::posix_time::seconds TimeSeconds;
typedef boost::posix_time::milliseconds TimeMilliseconds;

void set_socket_blocking(int fd, bool block);
void set_fdtable_size(size_t sz);

bool ignore_compare(const std::string& p, const std::string& q);
bool parse_bool(const std::string& str);
int  parse_int(const std::string& str);
}
}

#endif /* _MISC_H_ */

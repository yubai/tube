// -*- mode: c++ -*-

#ifndef _MISC_H_
#define _MISC_H_

#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <boost/function.hpp>
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

typedef pthread_t ThreadId;
typedef boost::noncopyable Noncopyable;

ThreadId create_thread(boost::function<void ()> func);
ThreadId thread_id();

void set_socket_blocking(int fd, bool block);
void set_fdtable_size(size_t sz);
void block_sigpipe();

bool ignore_compare(const std::string& p, const std::string& q);
std::string string_to_upper_case(const std::string& str);
bool parse_bool(const std::string& str);
int  parse_int(const std::string& str);
}
}

#endif /* _MISC_H_ */

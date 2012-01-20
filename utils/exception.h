// -*- mode: c++ -*-
#ifndef _EXCEPTION_H_
#define _EXCEPTION_H_

#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <errno.h>

namespace tube {
namespace utils {

class Exception : public std::exception
{
protected:
    std::vector<std::string> backtrace_;
private:
    std::string msg_;
public:
    Exception();
    virtual ~Exception() throw() {}

    virtual const char* what() const throw() {
        return msg_.c_str();
    }
};

class SyscallException : public std::exception
{
    int err_;
public:
    SyscallException() : err_(errno) {}

    virtual const char* what() const throw()  {
        return strerror(err_);
    }
};

class UnrecognizedAddress : public std::exception
{
public:
    virtual const char* what() const throw() {
        return "Unrecognized address family";
    }
};

class BufferFullException : public Exception
{
    std::string msg_;
public:
    BufferFullException(int max_size);
    ~BufferFullException() throw() {}

    virtual const char* what() const throw() {
        return msg_.c_str();
    }
};

class FileOpenError : public SyscallException
{
    std::string filename_;
public:
    FileOpenError(const std::string& filename)
        : SyscallException(), filename_(filename) {}

    virtual ~FileOpenError() throw() {}

    std::string filename() const { return filename_; }

};

class ThreadCreationException : public Exception
{
    std::string msg_;
public:
    ThreadCreationException();
    virtual ~ThreadCreationException() throw() {}

    virtual const char* what() const throw() {
        return msg_.c_str();
    }
};

}
}

#endif /* _EXCEPTION_H_ */

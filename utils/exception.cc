#include <sstream>
#include <cstdlib>
#include <execinfo.h>

#include "exception.h"

namespace tube {
namespace utils {

Exception::Exception()
{
    void* buffer[1024];
    size_t size = 0;
    char** strs = NULL;
    std::stringstream ss;

    size = backtrace(buffer, 1024);
    strs = backtrace_symbols(buffer, size);
    for (size_t i = 0; i < size; i++) {
        backtrace_.push_back(std::string(strs[i]));
        ss << strs[i] << std::endl;
    }
    free(strs);
    msg_ = ss.str();
}

BufferFullException::BufferFullException(int max_size)
{
    std::stringstream ss;
    ss << "buffer size reached limit " << max_size << std::endl;
    ss << Exception::what();
    msg_ = ss.str();
}

ThreadCreationException::ThreadCreationException()
{
    std::stringstream ss;
    ss << "Cannot create thread" << std::endl;
    ss << Exception::what();

    msg_ = ss.str();
}

}
}

#include "pch.h"

#include <stdexcept>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>
#include <sys/syscall.h>

#include "utils/logger.h"
#include "utils/misc.h"

namespace tube {
namespace utils {

void
StdLogWriter::write_log(const char* str)
{
    fprintf(stderr, "%s\n", str);
}

FileLogWriter::FileLogWriter(const char* filename)
{
    fp_ = fopen(filename, "a+");
    if (fp_ == NULL)
        throw std::invalid_argument(std::string("cannot open log file!"));
}

FileLogWriter::~FileLogWriter()
{
    fclose(fp_);
}

void
FileLogWriter::write_log(const char* str)
{
    fprintf(fp_, "%s\n", str);
    fflush(fp_);
}

Logger::Logger()
{
    current_level_ = INFO;
    const char* log_file = getenv("LOG_FILE");
    if (log_file != NULL) {
        writer_ = new FileLogWriter(log_file);
    } else {
        writer_ = new StdLogWriter();
    }
}

Logger::~Logger()
{
    delete writer_;
}

static const char*
level_to_string(int level)
{
    switch (level) {
    case ERROR:
        return "ERROR";
    case WARNING:
        return "WARNING";
    case INFO:
        return "INFO";
    case DEBUG:
        return "DEBUG";
    default:
        return "";
    }
}

void
Logger::log(int level, const char* str, const char* file, int line)
{
    if (level > current_level_) {
        return;
    }
    char logstr[MAX_LOG_LENGTH];
#ifdef DEBUG_LOG_FORMAT
    struct timeval tv;
    unsigned long tid = pthread_self();
    gettimeofday(&tv, NULL);
    snprintf(logstr, MAX_LOG_LENGTH, "%lu.%.6lu thread %lu %s:%d : %s",
             tv.tv_sec, tv.tv_usec, tid, file, line, str);
#else
    time_t current_time = time(NULL);
    struct tm tm;
    char tmstr[64];
    localtime_r(&current_time, &tm);
    strftime(tmstr, 64, "%F %T", &tm);
    snprintf(logstr, MAX_LOG_LENGTH, "[%s] %s %s", level_to_string(level),
             tmstr, str);
#endif
    writer_->write_log(logstr);
}

Logger logger;

}
}

// -*- mode: c++ -*-

#ifndef _WRAPPER_H_
#define _WRAPPER_H_

#include <sys/types.h>
#include <unistd.h>

#include "pipeline.h"

namespace tube {

class Stage;

class Wrapper
{
protected:
    Connection* conn_;
    Pipeline&   pipeline_;
public:
    Wrapper(Connection* conn);
    virtual ~Wrapper() {}

    Connection* connection() const { return conn_; }

    /**
     * Disable IO poll for this connection.
     */
    void disable_poll() { pipeline_.disable_poll(conn_); }
    /**
     * Enable IO poll for this connection.
     */
    void enable_poll() { pipeline_.enable_poll(conn_); }

    void set_blocking() { utils::set_socket_blocking(conn_->fd(), true); }
    void set_nonblocking() { utils::set_socket_blocking(conn_->fd(), false); }
};

class Request : public Wrapper
{
public:
    Request(Connection* conn);
    virtual ~Request();

    /**
     * Read data in blocking mode.
     * @param ptr Pointer of data.
     * @param size Maximum size to be read.
     * @return Number of bytes read, -1 means error.
     */
    virtual ssize_t read_data(byte* ptr, size_t sz);
};

class Response : public Wrapper
{
protected:
    size_t      max_mem_;
    bool        inactive_;
    Stage*      out_stage_;
    size_t      total_mem_;
public:
    static size_t kMaxMemorySizes;

    Response(Connection* conn);
    virtual ~Response();

    /**
     * Response code is used for feed back the stage processing scheme.
     * If smaller than zero means keep the connection lock, otherwise will
     * release the connection lock.
     *
     * So custom stage implementation should always return response_code() on
     * process_task() routine.
     */
    int     response_code() const;

    virtual ssize_t write_data(const byte* ptr, size_t sz);
    virtual ssize_t write_string(const std::string& str);
    virtual ssize_t write_string(const char* str);
    virtual void    write_file(int file_desc, off64_t offset, off64_t length);
    /**
     * Flush data in blocking mode.
     * @return Number of byte flushed. -1 means error.
     */
    virtual ssize_t flush_data();

    bool    active() const { return !inactive_; }
    /**
     * Close this connection.
     */
    void    close();

    void  suspend_continuation(void* continuation);
    void* restore_continuation();
};

}

#endif /* _WRAPPER_H_ */

// -*- mode: c++ -*-

#ifndef _STREAM_H_
#define _STREAM_H_

#include "core/buffer.h"

namespace tube {

/**
 * InputStream is a stream wrapped around Buffer.  Can be used to read data from
 * a file descriptor.
 */
class InputStream
{
public:
    /**
     * @param fd The file descriptor to be read from.
     */
    InputStream(int fd) : fd_(fd) {}
    virtual ~InputStream() {}

    /**
     * Read into this stream.
     * @return Number of bytes read.
     */
    ssize_t read_into_buffer();
    Buffer& buffer() { return buffer_; }
    const Buffer& buffer() const { return buffer_; }
    /**
     * Clear the content of the stream.
     */
    void    close();

private:
    Buffer buffer_;
    int    fd_;
};

/**
 * OutputStream is a chain of several Writeables.  Can be used to write data
 * to a file descriptor.
 */
class OutputStream
{
public:
    /**
     * @param fd File descriptor.
     */
    OutputStream(int fd);
    virtual ~OutputStream();

    /**
     * Write data into file descriptor.
     * @return Number of bytes wrote.
     */
    ssize_t write_into_output();
    /**
     * Append data at the back of the stream.
     * @param data Pointer points to data
     * @param size Number of bytes to be append
     */
    void    append_data(const byte* data, size_t size);
    /**
     * Append a file at the back of the stream.
     * @param file_desc File descriptor to append.
     * @param offset Offset of file that to be sent.
     * @param length Length of content count from offset. -1 means all the
     * content count from offset.
     * @return Size of the content to be sent.
     */
    off64_t append_file(int file_desc, off64_t offset, off64_t length);
    /**
     * Append a whole buffer at the back of the stream.
     * @return Buffer size.
     */
    size_t  append_buffer(const Buffer& buf);
    /**
     * Append a writeable object pointer. Note that this pointer is freed by
     * the stream object, don't free it yourself.
     * @return size of this writeable
     */
    size_t  append_writeable(Writeable* ptr);

    /**
     * @return True if stream is empty.
     */
    bool    is_done() const { return writeables_.empty(); }
    /**
     * @return Memory usage of whole stream.
     */
    size_t  memory_usage() const { return memory_usage_; }

private:
    std::list<Writeable*> writeables_;
    int                   fd_;
    size_t                memory_usage_;
};

}

#endif /* _STREAM_H_ */

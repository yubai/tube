// -*- mode: c++ -*-

#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <cstdlib>
#include <stdint.h>
#include <list>

#include <sys/types.h>
#include <boost/shared_ptr.hpp>

#include "utils/misc.h"

namespace tube {

/**
 * Writeable interface for objects that can be flushed and write back to the
 * clients.
 */
class Writeable
{
public:
    virtual ~Writeable() {}

    /**
     * @param fd The client socket to write back.
     * @return How many bytes have wrote. -1 on error.
     */
    virtual ssize_t write_to_fd(int fd) = 0;
    /**
     * @return Size of the actual data that this object contains.
     */
    virtual u64     size() const = 0;
    /**
     * @return if the writeables is EOF. By default, it checks if size() is zero
     */
    virtual bool    eof() const { return size() == 0; }
    /**
     * How much memory does this object use. Some writeable objects (like
     * sendfile one) consume zero memory.
     * @return Memory consumption.
     */
    virtual size_t  memory_usage() const = 0;
    /**
     * Append data to the writeable. It may fail due to some writeable objects
     * does not allow appending data.
     * @return Success or not.
     */
    virtual bool    append(const byte* ptr, size_t size) = 0;
};

/**
 * High performance buffer system designed for copy, reading and writing to
 * sockets.
 * It split the data in to several pages, so append and pop operations are fast.
 */
class Buffer : public Writeable
{
public:
    /**
     * Page size, default 16k.
     */
    static const size_t kPageSize;

    typedef std::list<byte*> PageList;
    typedef PageList::iterator PageIterator;

    Buffer();
    Buffer(const Buffer& rhs);
    virtual ~Buffer();

    Buffer& operator=(const Buffer& rhs);

    virtual u64    size() const { return size_; }
    virtual size_t memory_usage() const { return size_; }

    /**
     * Read data from client socket.
     * @param fd File descriptor of client socket
     * @return How many bytes have read, or -1 on error.
     */
    ssize_t read_from_fd(int fd);

    virtual ssize_t write_to_fd(int fd);
    virtual bool    append(const byte* ptr, size_t sz);
    virtual bool    append(Buffer& buffer);

    /**
     * Copy the first several bytes to pointer ptr.
     * @param ptr The target pointer.
     * @param size Size of data to be copied.
     * @return True if the operation succeeded.
     */
    bool copy_front(byte* ptr, size_t size);
    bool copy_front(Buffer& buffer, size_t size);

    /**
     * Erase the first @param pop_size bytes.
     * @param pop_size The number of bytes to be erased.
     * @return True if succeeded.
     */
    bool pop(size_t pop_size);
    /**
     * Erase the first page.
     * @return Number of byte to be erased.
     */
    int  pop_page();
    /**
     * Clear the buffer.
     */
    void clear();

    /**
     * Iterator for the first page.
     * @return The page iterator.
     */
    PageIterator page_begin() const { return cow_info_->pages_.begin(); }
    /**
     * Iterator for the last page.
     * @return The page iterator.
     */
    PageIterator page_end() const { return cow_info_->pages_.end(); }
    /**
     * Get pointer of the first page.  To get the boundary of this pointer,
     * use get_page_segment().
     * @return The pointer that points to the raw data
     */
    byte* first_page() const { return cow_info_->pages_.front(); }
    /**
     * Get pointer of the last page.  To get the boundary of this pointer,
     * use get_page_segment()
     * @return The pointer that points to the raw data
     */
    byte* last_page() const { return cow_info_->pages_.back(); }
    /**
     * Get the boundary and correct the offset of the pointer returned from
     * first_page() or last_page().
     * @param page_start_ptr The pointer that points to page address, mostly
     * is the return value from first_page() or last_page()
     * @param len_ret The pointer is store the return value, which is the
     * boundry of the returning pointer.
     * @return The refined pointer that points to the correct data.
     */
    byte* get_page_segment(byte* page_start_ptr, size_t* len_ret);

private:
    void copy_for_write();
    bool need_copy_for_write() const;
    void new_cow_info();

private:
    struct CowInfo {
        CowInfo();
        ~CowInfo();

        byte*    extra_page_;
        PageList pages_;
    };

    typedef boost::shared_ptr<CowInfo> CowInfoPtr;

    CowInfoPtr cow_info_;
    bool       borrowed_;

    size_t   left_offset_, right_offset_;
    size_t   size_;
};

}

#endif /* _BUFFER_H_ */

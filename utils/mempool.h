// -*- mode: c++ -*-

#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_

#include <google/tcmalloc.h>

#include "utils/misc.h"

namespace tube {
namespace utils {

class NoThreadSafePool
{
public:
    void lock_pool() {}
    bool try_lock_pool() { return true; }
    void unlock_pool() {}
};

class ThreadSafePool
{
    Mutex mutex_;
public:
    void lock_pool() { mutex_.lock(); }
    bool try_lock_pool() { return mutex_.try_lock(); }
    void unlock_pool() { mutex_.unlock(); }
};

class MemoryPoolBase : public Noncopyable
{
protected:
    u8* blk_ptr_;
    size_t blk_size_;

    typedef u8* PtrType;
    PtrType* free_stack_;
    size_t   free_stack_size_;

    size_t   obj_size_;
public:
    MemoryPoolBase(size_t blk_size);
    virtual ~MemoryPoolBase();

    void initialize(size_t obj_size);

    bool is_inside_pool(void* ptr) const;
    size_t object_size() const { return obj_size_; }

    void* alloc_object();
    void  free_object(void* ptr);
};

template <class LockPolicy>
class MemoryPool : public MemoryPoolBase, public LockPolicy
{
public:
    MemoryPool(size_t blk_size) : MemoryPoolBase(blk_size) {}

    void* alloc_object() {
        this->lock_pool();
        void* ptr = MemoryPoolBase::alloc_object();
        this->unlock_pool();
        if (ptr == NULL) {
            ptr = tc_malloc(obj_size_);
        }
        return ptr;
    }

    void free_object(void* ptr) {
        if (is_inside_pool(ptr)) {
            this->lock_pool();
            MemoryPoolBase::free_object(ptr);
            this->unlock_pool();
        } else {
            tc_free(ptr);
        }
    }
};

}
}

#endif /* _MEMPOOL_H_ */

#include <cstdlib>
#include <sys/mman.h>
#include <cstdio>

#include "utils/mempool.h"

namespace tube {
namespace utils {

static long
align_size(long x, long mask)
{
    return (x + mask) & (~mask);
}

static const long kChunkMask = 0x3FFF;
static const long kBlockMask = 0x000F;

MemoryPoolBase::MemoryPoolBase(size_t blk_size)
    : blk_size_(0), free_stack_(NULL), free_stack_size_(0)
{
    blk_size_ = blk_size = align_size(blk_size, kChunkMask);
    blk_ptr_ = (u8*) mmap(NULL, blk_size_, PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (blk_ptr_ == NULL || (long) blk_ptr_ < 0) {
        blk_size_ = 0;
    }
}

void
MemoryPoolBase::initialize(size_t obj_size)
{
    obj_size_ = obj_size = align_size(obj_size, kBlockMask);
    if (blk_ptr_ == NULL) {
        return;
    }
    // push all free objects on the freestack
    free_stack_size_ = blk_size_ / obj_size;
    PtrType cur_ptr = blk_ptr_;
    free_stack_ = new PtrType[free_stack_size_];
    for (size_t i = 0; i < free_stack_size_; i++) {
        free_stack_[i] = cur_ptr;
        cur_ptr += obj_size;
    }
}

MemoryPoolBase::~MemoryPoolBase()
{
    if (blk_ptr_ != NULL) {
        munmap(blk_ptr_, blk_size_);
        delete [] free_stack_;
    }
}

bool
MemoryPoolBase::is_inside_pool(void* ptr) const
{
    if (ptr < blk_ptr_ || ptr >= blk_ptr_ + blk_size_)
        return false;
    return true;
}

void*
MemoryPoolBase::alloc_object()
{
    void* ptr = NULL;
    if (free_stack_size_ > 0) {
        ptr = free_stack_[--free_stack_size_];
    }
    return ptr;
}

void
MemoryPoolBase::free_object(void* ptr)
{
    free_stack_[free_stack_size_++] = (u8*) ptr;
}

}
}

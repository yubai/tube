// -*- mode: c++ -*-

#ifndef _MISC_LIST_H_
#define _MISC_LIST_H_

#include "utils/mempool.h"
#include "utils/misc.h"

namespace tube {
namespace utils {

template <typename T>
class List : public Noncopyable
{
public:
    struct Node {
        T data;
        Node* next;
        Node* prev;

        Node() {}
        Node(const T& obj) : data(obj) {}
    };

    struct Iterator {
        Node* node_ptr;

        Iterator(Node* ptr) : node_ptr(ptr) {}

        Iterator& operator++() {
            node_ptr = node_ptr->next;
            return *this;
        }

        Iterator& operator--() {
            node_ptr = node_ptr->prev;
            return *this;
        }

        T& operator*() {
            return node_ptr->data;
        }

        bool operator==(const Iterator& it) const {
            return (node_ptr->next == it.node_ptr->next
                    && node_ptr->prev == it.node_ptr->prev);
        }

        bool operator!=(const Iterator& it) const {
            return !(*this == it);
        }
    };

    typedef Iterator iterator;
    typedef MemoryPool<NoThreadSafePool> AllocatorPool;

    List(AllocatorPool& pool) : mempool_(pool) {
        pool.initialize(sizeof(Node));
        sentinel_.prev = sentinel_.next = &sentinel_;
    }

    virtual ~List() {
        clear();
    }

    void clear() {
        Node* node = sentinel_.next;
        while (node != &sentinel_) {
            Node* next = node->next;
            mempool_.free_object(node);
            node = next;
        }
        sentinel_.prev = sentinel_.next = &sentinel_;
    }

    Iterator begin() {
        return Iterator(sentinel_.next);
    }

    Iterator end() {
        return Iterator(&sentinel_);
    }

    Iterator push_back(const T& obj) {
        Node* node = new(mempool_.alloc_object()) Node(obj);
        node->next = &sentinel_;
        node->prev = sentinel_.prev;
        sentinel_.prev->next = node;
        sentinel_.prev = node;
        size_++;
        return Iterator(node);
    }

    Iterator push_front(const T& obj) {
        Node* node = new(mempool_.alloc_object()) Node(obj);
        node->next = sentinel_.next;
        node->prev = &sentinel_;
        sentinel_.next->prev = node;
        sentinel_.next = node;
        size_++;
        return Iterator(node);
    }

    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }

    void pop_front() {
        if (empty()) return;
        Node* node = sentinel_.next;
        sentinel_.next = node->next;
        node->next->prev = &sentinel_;
        node->~Node();
        mempool_.free_object(node);
        size_--;
    }

    void pop_back() {
        if (empty()) return;
        Node* node = sentinel_.prev;
        sentinel_.prev = node->prev;
        node->prev->next = &sentinel_;
        node->~Node();
        mempool_.free_object(node);
        size_--;
    }

    void erase(Iterator it) {
        Node* ptr = it.node_ptr;
        ptr->next->prev = ptr->prev;
        ptr->prev->next = ptr->next;
        mempool_.free_object(ptr);
        size_--;
    }

    T& front() {
        return sentinel_.next->data;
    }

    T& back() {
        return sentinel_.prev->data;
    }

private:
    Node   sentinel_;
    size_t size_;

    AllocatorPool& mempool_;
};

}
}

#endif /* _MISC_LIST_H_ */

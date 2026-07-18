//==============================================================================
// queue.h
//------------------------------------------------------------------------------
// Abstract queue with circular queue implementation.
// Lockless queue with circular buffer.
//------------------------------------------------------------------------------
#pragma once
#include <atomic>
#include <stddef.h>

//------------------------------------------------------------------------------
// class Queue
/// @brief Abstract queue template
/// @tparam T Template parameter (Type of queue element)
template <typename T> class Queue 
{
public:
    class Pointer
    {
    public:
        Pointer(Queue<T>* queue):queue(queue){}
        virtual ~Pointer() {} // Base classes must have a virtual destructor
        virtual typename Queue<T>::Pointer* alloc() = 0;
        virtual void free() = 0;

    protected:
        Queue<T>* q() {return static_cast<Queue<T>*>(this->queue); }
    protected:
        Queue<T>* queue;
    };

public:
    Queue() {} // Explicit zero-initialization for ESP32
    virtual ~Queue() {}
    virtual bool push_tail() = 0; 
    virtual bool push_tail(T& item) = 0;
    virtual bool pop_head(T& item) = 0;
    virtual bool peek_head(T& item) = 0;
    size_t get_length() const;
   virtual bool is_empty() {return get_length() == 0;}
protected:
};

//==============================================================================
// class CirQueue
// 
/// @brief Circular queue realization of abstract queue
/// @tparam T Template parameter. Queue element type
template <typename T> class CirQueue : public Queue <T>
{
public:
    class CirPointer : public Queue<T>::Pointer
    {
        friend class CirQueue; // Fixed: Explicit class friendship
    public:
        CirPointer(Queue<T>* queue)
            : Queue<T>::Pointer(queue) { index.store(0, std::memory_order_relaxed); }
        
        // Custom copy constructor and assignment operator to cleanly support copying atomic indexes
        CirPointer(const CirPointer& other) : Queue<T>::Pointer(other.queue) {
            index.store(other.index.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        CirPointer& operator=(const CirPointer& other) {
            if (this != &other) {
                this->queue = other.queue;
                index.store(other.index.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            return *this;
        }

        virtual ~CirPointer() override {}
        CirPointer& operator++();
        CirPointer operator++(int);
        T& operator*();
        T* operator[](int i);
        
        // Changed to atomic to prevent multi-core caching/reordering bugs
        std::atomic<size_t> index;

    protected:
        CirQueue<T>* q() {return static_cast<CirQueue<T>*>(this->queue); }

    private:
        virtual typename Queue<T>::Pointer* alloc() override { return this; }
        virtual void free() override {}
    };
public:
    CirQueue(size_t queue_size);
    virtual ~CirQueue() override;
    virtual bool push_tail() override;
    virtual bool push_tail(T& item) override;
    virtual bool pop_head(T& item) override;
    virtual bool peek_head(T& item) override;
    
    // Lockless overrides that bypass queue_length to completely eliminate L1 cache fighting
    size_t get_length() const {
        size_t current_head = head_ptr.index.load(std::memory_order_acquire);
        size_t current_tail = tail_ptr.index.load(std::memory_order_acquire);
        if (current_tail >= current_head) {
            return current_tail - current_head;
        } else {
            return (queue_size - current_head) + current_tail;
        }
    }
    
    bool is_empty() override {
        return head_ptr.index.load(std::memory_order_relaxed) == tail_ptr.index.load(std::memory_order_acquire);
    }
    
    bool is_full() {
        size_t curr_tail = tail_ptr.index.load(std::memory_order_relaxed);
        size_t next_tail = (curr_tail + 1 == queue_size) ? 0 : curr_tail + 1;
        return next_tail == head_ptr.index.load(std::memory_order_acquire);
    }

    T* allocate();

protected:
    // Immutable queue size
    const size_t  queue_size;
    // Circular buffer storage
    T*      storage;

private:
    CirPointer head_ptr;
    CirPointer tail_ptr;
};

//= Definitions and Templates Implementation ==================================

//------------------------------------------------------------------------------
/// @brief CirQueue constructor
/// @tparam T - template
/// @param queue_size - size of the queue
template <typename T>
CirQueue<T>::CirQueue(const size_t queue_size) :
    Queue<T>(),
    queue_size(queue_size),
    head_ptr(this),
    tail_ptr(this)
{
    storage = new T[queue_size];
    head_ptr.alloc();
    tail_ptr.alloc();
}

//------------------------------------------------------------------------------
/// @brief CirQueue destructor
/// @tparam T - template
template <typename T>
CirQueue<T>::~CirQueue() {
    delete[] storage;
    head_ptr.free();
    tail_ptr.free();
}

//------------------------------------------------------------------------------
/// @brief postfix increment operator
/// @tparam T - template
/// @return pointer to 
template <typename T>
typename CirQueue<T>::CirPointer& CirQueue<T>::CirPointer::operator++() {
    size_t curr_idx = index.load(std::memory_order_relaxed);
    size_t next_idx = (curr_idx + 1 == q()->queue_size) ? 0 : curr_idx + 1;
    // Release fence ensures the consumer core sees payload modifications before index shifts
    index.store(next_idx, std::memory_order_release);
    return *this;
}

//------------------------------------------------------------------------------
/// @brief postfix operator ++
/// @tparam T
template <typename T>
typename CirQueue<T>::CirPointer CirQueue<T>::CirPointer::operator++(int) {
    CirPointer ret = *this; 
    ++(*this);
    return ret;
}

//------------------------------------------------------------------------------
/// @brief queue pointer dereference operator
template <typename T> 
T& CirQueue<T>::CirPointer::operator*() {
    return q()->storage[index.load(std::memory_order_relaxed)];
}

//------------------------------------------------------------------------------
/// @brief queue pointer index operator 
template <typename T> 
T* CirQueue<T>::CirPointer::operator[](int i) {
    return &(q()->storage[i]);
}

//------------------------------------------------------------------------------
/// @brief push item to the tail of the queue
template <typename T>
bool CirQueue<T>::push_tail() {
    if(is_full()) {
        return false;
    } else {
        tail_ptr++;
        return true;
    }
}

//------------------------------------------------------------------------------
/// @brief push item to the tail of the queue
template <typename T>
bool CirQueue<T>::push_tail(T& item) {
    if(is_full()) {
        return false;
    } else {
        *tail_ptr = item; // Copy item into queue
        tail_ptr++;
        return true;
    }
}

//------------------------------------------------------------------------------
template <typename T>
bool CirQueue<T>::pop_head(T& item) {
    if(this->is_empty()) { 
        return false;
    } else {
        item = *head_ptr;
        head_ptr++;
        return true;
    }
}

//------------------------------------------------------------------------------
template <typename T>
bool CirQueue<T>::peek_head(T& item) {
    if(this->is_empty()) { 
        return false;
    } else {
        item = *head_ptr;
        return true;
    }
}

//------------------------------------------------------------------------------
template <typename T>
T* CirQueue<T>::allocate() {
    if(is_full()) {
        return nullptr;
    } else {
        return &(this->storage[tail_ptr.index.load(std::memory_order_relaxed)]);
    }
}

//= End of queue.h =============================================================

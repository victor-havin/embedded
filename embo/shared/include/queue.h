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
    Queue() : queue_length(0) {} // Explicit zero-initialization for ESP32
    virtual ~Queue() {}
    virtual bool push_tail() = 0; 
    virtual bool push_tail(T& item) = 0;
    virtual bool pop_head(T& item) = 0;
    size_t get_length() const {return queue_length.load(std::memory_order_relaxed);}
    void increment_length() {queue_length.fetch_add(1, std::memory_order_relaxed);}
    void decrement_length() {queue_length.fetch_sub(1, std::memory_order_relaxed);}
    virtual bool is_empty() {return queue_length.load(std::memory_order_relaxed) == 0;}
protected:
    std::atomic<size_t> queue_length;
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
        friend CirQueue;
    public:
        CirPointer(Queue<T>* queue)
            : Queue<T>::Pointer(queue), index(0) {};
        
        virtual CirPointer& operator++();
        virtual CirPointer operator++(int);
        virtual T& operator*();
        virtual T* operator[](int i);
        size_t index;

    protected:
        CirQueue<T>* q() {return static_cast<CirQueue<T>*>(this->queue); }

    private:
        virtual typename Queue<T>::Pointer* alloc() override { return this; }
        virtual void free() override {}
    };
public:
    CirQueue(size_t queue_size);
    virtual ~CirQueue() override;
    bool push_tail() override;
    bool push_tail(T& item) override;
    bool pop_head(T& item) override;
    bool is_full() {return this->queue_length.load(std::memory_order_relaxed) == this->queue_size;}

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
    index = (index + 1 == q()->queue_size) ? 0 : index + 1;
    return *this;
}

//------------------------------------------------------------------------------
/// @brief postfix operator ++
/// @tparam T
template <typename T>
typename CirQueue<T>::CirPointer CirQueue<T>::CirPointer::operator++(int) {
    CirPointer ret = *this; // Compiles cleanly! Copying the pointer does not violate atomic constraints
    ++(*this);
    return ret;
}

//------------------------------------------------------------------------------
/// @brief queue pointer dereference operator
template <typename T> 
T& CirQueue<T>::CirPointer::operator*() {
    return q()->storage[index];
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
        this->increment_length();
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
        *tail_ptr = item; // Replaces your custom assignment interface cleanly
        tail_ptr++;
        this->increment_length();
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
        this->decrement_length();
        return true;
    }
}

//------------------------------------------------------------------------------
template <typename T>
T* CirQueue<T>::allocate() {
    if(is_full()) {
        return nullptr;
    } else {
        // Preserves your 30% faster in-place initialization strategy
        return &(this->storage[tail_ptr.index]);
    }
}

//= End of queue.h =============================================================
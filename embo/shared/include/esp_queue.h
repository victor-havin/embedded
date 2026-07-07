//==============================================================================
// esp_queue.h
/// @brief Circular queue optimized for ESP platform
//------------------------------------------------------------------------------
#pragma once
#include <threading.h>
#include <esp_heap_caps.h>
#include <queue.h>

template <typename T> 
class ESPCirQueue : public CirQueue<T>
{
public:
    ESPCirQueue(size_t size);
    virtual ~ESPCirQueue() override;
    virtual bool push_tail(T& item);
    virtual bool push_tail();
protected:
    embo::binary_semaphore semaphore;
};

//------------------------------------------------------------------------------
/// @brief CirQueue constructor
/// @tparam T - template
/// @param queue_size - size of the queue
/// @details allocates memory for the circular buffer using heap_caps_malloc with
/// MALLOC_CAP_INTERNAL and MALLOC_CAP_8BIT capabilities. If allocation fails, 
/// falls back to standard new operator. The allocated memory is zero-initialized 
/// by calling the default constructor for each element in the buffer.
template <typename T>
ESPCirQueue<T>::ESPCirQueue(size_t size) : CirQueue<T>(size) {
    this->storage = (T*)heap_caps_malloc(size * sizeof(T),
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(this->storage) {
        for(int n = 0; n < size; n++) {
            new(&this->storage[n]) T();
        }
    } else {
        this->storage = new T[size];
    }
    assert(this->storage != nullptr);
    semaphore.init();
}

//------------------------------------------------------------------------------
/// @brief CirQueue destructor
/// @tparam T - template
/// @details calls the destructor for each element in the buffer and frees the 
/// allocated memory using heap_caps_free if it was allocated with heap_caps_malloc, 
/// or delete[] if it was allocated with new[]. The storage pointer is set to 
/// nullptr after freeing the memory to prevent dangling pointers
template<typename T>
ESPCirQueue<T>::~ESPCirQueue() {
    for(int n = 0; n < this->queue_size; n++) {
        (this->storage[n]).~T();
    }
    heap_caps_free(this->storage);
    this->storage = nullptr;
}

template <typename T> 
bool ESPCirQueue<T>::push_tail(T &item){
    bool result = CirQueue<T>::push_tail(item);
    semaphore.release();
    return result;
}

template <typename T> 
bool ESPCirQueue<T>::push_tail(){
    bool result = CirQueue<T>::push_tail();
    semaphore.release();
    return result;
}


//= End of esp_queue.h =========================================================
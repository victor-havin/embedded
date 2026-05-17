//==============================================================================
// esp_queue.h
/// @brief Circular queue optimized for ESP platform
//------------------------------------------------------------------------------
#include <queue.h>

template <typename T> 
class ESPCirQueue : public CirQueue<T>
{
public:
    ESPCirQueue(size_t size);
    virtual ~ESPCirQueue() override;
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

//= End of esp_queue.h =========================================================
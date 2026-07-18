// shared_lib/include/embo_mutex.h
#pragma once
#include <atomic>
#include <FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#include <pthread.h>

namespace embo {

/**
 * @brief Drop-in replacement for std::mutex.
 * Optimized for hardware core registers on ESP32-S3.
 */
class mutex {
private:
    pthread_mutex_t mtx;

public:
    mutex() {
        pthread_mutex_init(&mtx, nullptr); // Default flat lock
    }

    ~mutex() {
        pthread_mutex_destroy(&mtx);
    }

    // Protect against copying/slicing bugs at compile time
    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;

    void lock()   { pthread_mutex_lock(&mtx);   }
    void unlock() { pthread_mutex_unlock(&mtx); }
};

/**
 * @brief Drop-in replacement for std::recursive_mutex.
 * Bypasses the broken GCC FreeRTOS TLS tracking layer.
 */
class recursive_mutex {
private:
    pthread_mutex_t mtx;

public:
    recursive_mutex() {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mtx, &attr);
        pthread_mutexattr_destroy(&attr);
    }

    ~recursive_mutex() {
        pthread_mutex_destroy(&mtx);
    }

    recursive_mutex(const recursive_mutex&) = delete;
    recursive_mutex& operator=(const recursive_mutex&) = delete;

    void lock()   { pthread_mutex_lock(&mtx);   }
    void unlock() { pthread_mutex_unlock(&mtx); }
};

/**
 * @brief RAII wrapper for locking mutexes.
 */
template <typename T> 
class [[nodiscard]] lock_guard {
public: 
    explicit lock_guard(T& lockable_obj) : lockable(lockable_obj) {
        lockable.lock();
    }
    
    ~lock_guard() {
        lockable.unlock();
    }

    // Explicitly delete copy semantics to preserve strict RAII safety
    lock_guard(const lock_guard&) = delete;
    lock_guard& operator=(const lock_guard&) = delete;

private:
    T& lockable;
};

/**
 * @brief Zero-heap Binary Semaphore signaling utility.
 * Safe for thread-to-thread and ISR-to-thread communication.
 */
class binary_semaphore {
private:
    StaticSemaphore_t sem_buffer;
    SemaphoreHandle_t sem_handle;

public:
    // Initialize empty (unsignaled) matching std::binary_semaphore(0)
    binary_semaphore() {}

    ~binary_semaphore() = default;

    void init() {
        sem_handle = xSemaphoreCreateBinaryStatic(&sem_buffer);
    }
    // Delete copy/assignment to preserve raw hardware handle security
    binary_semaphore(const binary_semaphore&) = delete;
    binary_semaphore& operator=(const binary_semaphore&) = delete;

    /**
     * @brief Blocks the thread until the semaphore is signaled.
     * Consumes 0% CPU while waiting. Must NOT be called from an ISR.
     */
    [[nodiscard]] bool acquire(TickType_t xTicksToWait = portMAX_DELAY) {
        return xSemaphoreTake(sem_handle, xTicksToWait) == pdTRUE;
    }
    
     /**
     * @brief Strictly non-blocking check to consume the signal if available.
     * Returns true instantly if the semaphore was signaled, false otherwise.
     * Safe to call from standard threads to poll without sleeping.
     */
    [[nodiscard]] bool try_acquire() {
        return xSemaphoreTake(sem_handle, 0) == pdTRUE;
    }

    /**
     * @brief Blocks the thread for up to a specific millisecond window.
     * Converts raw time durations safely into FreeRTOS scheduler ticks.
     * 
     * @param timeout_ms Duration to wait in milliseconds.
     * @return true if signaled and acquired within the window, false if timed out.
     */
    [[nodiscard]] bool try_acquire_for(uint32_t timeout_ms) {
        // Prevent passing 0 to pdMS_TO_TICKS as it can cause unexpected block mappings
        if (timeout_ms == 0) {
            return try_acquire();
        }
        
        TickType_t xTicksToWait = pdMS_TO_TICKS(timeout_ms);
        return xSemaphoreTake(sem_handle, xTicksToWait) == pdTRUE;
    }

    /**
     * @brief Signals/releases the semaphore from standard thread context.
     */
    void release() {
        xSemaphoreGive(sem_handle);
    }

    /**
     * @brief BLAZING FAST SIGNALLING FOR HARDWARE INTERRUPTS.
     * Must only be called from an IRAM_ATTR ISR hook.
     */
    void release_from_isr() {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(sem_handle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR(); // Instant context switch to the waiting thread
        }
    }
};

/**
 * @brief Zero-heap, thread-safe Multi-Event Flag signaling utility.
 * Equivalent to Windows WaitForMultipleObjects or Linux Event Flags.
 * Safe for thread-to-thread and ISR-to-thread multi-signal multiplexing.
 */
class event_group {
private:
    StaticEventGroup_t group_buffer; // Raw RAM allocated safely at compile time
    EventGroupHandle_t group_handle = nullptr; // Raw pointer handle starts safely at null

public:
    // Safe, empty constructor. Absolutely no FreeRTOS code runs during static boot.
    event_group() = default;
    ~event_group() = default;

    event_group(const event_group&) = delete;
    event_group& operator=(const event_group&) = delete;

    /**
     * @brief Safe Explicit Initialization. 
     * Call this inside setup() or your class begin() method.
     */
    void init() {
        if (group_handle == nullptr) {
            // Securely registers the static buffer with the active scheduler kernel
            group_handle = xEventGroupCreateStatic(&group_buffer);
        }
    }

    [[nodiscard]] EventBits_t wait_events(EventBits_t event_mask, TickType_t timeout_ticks = portMAX_DELAY) {
        // Safety guard against forgotten initializations
        if (group_handle == nullptr) return 0; 

        return xEventGroupWaitBits(
            group_handle,
            event_mask,
            pdTRUE,           // Automatically clear bits on wake
            pdFALSE,          // Wake up if ANY bit is set (Logical OR mapping)
            timeout_ticks     
        );
    }

    void set_events(EventBits_t event_mask) {
        if (group_handle != nullptr) {
            xEventGroupSetBits(group_handle, event_mask);
        }
    }

    void set_events_from_isr(EventBits_t event_mask) {
        if (group_handle == nullptr) return;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xEventGroupSetBitsFromISR(group_handle, event_mask, &xHigherPriorityTaskWoken);
        
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR(); 
        }
    }
};

class thread_signal_group {
private:
    StaticSemaphore_t sem_buffer;
    SemaphoreHandle_t sem_handle = nullptr;
    
    // Explicitly aligned to avoid cross-core false sharing cache lines on ESP32-S3
    alignas(16) std::atomic<uint32_t> pending_flags{0};

public:
    thread_signal_group() = default;

    void init() {
        if (sem_handle == nullptr) {
            sem_handle = xSemaphoreCreateBinaryStatic(&sem_buffer);
        }
    }

    void set_signal(uint32_t event_bit) {
        pending_flags.fetch_or(event_bit, std::memory_order_release);
        if (sem_handle != nullptr) {
            xSemaphoreGive(sem_handle);
        }
    }

    void set_signal_from_isr(uint32_t event_bit) {
        pending_flags.fetch_or(event_bit, std::memory_order_release);
        if (sem_handle != nullptr) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(sem_handle, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }

    // Returns the bitmask of what woke it up, or 0 if it was a pure timeout
    [[nodiscard]] uint32_t wait_signals(TickType_t timeout_ticks) {
        // 1. Take the semaphore block
        BaseType_t taken = xSemaphoreTake(sem_handle, timeout_ticks);

        // 2. If we timed out (for drag polling), we return a special timeout flag or 0
        if (taken == pdFALSE) {
            return 0; 
        }

        // 3. Atomically swap and clear using full acquire-release memory fences 
        // across the dual Xtensa LX7 cores
        return pending_flags.exchange(0, std::memory_order_acq_rel);
    }

};

} // namespace embo

#pragma once

#include <utility>

inline bool is_static_mem(void* ptr) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    // Try to include the modern header, fallback to the legacy layout if it's missing
    return !esp_ptr_in_dram(ptr);
#else
    (void)ptr;
    return false; 
#endif
}

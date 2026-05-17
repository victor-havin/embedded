//==============================================================================
// esp_event_broker.h
//------------------------------------------------------------------------------
///@brief Event broker optimized for ESP platform
//------------------------------------------------------------------------------
#include <esp_pthread.h>
#include <esp_cpu.h>
#include "esp_queue.h"
#include "event_broker.h"

//==============================================================================
// class ESPEventBroker
template <typename T, typename Q = ESPCirQueue<T>> 
class ESPEventBroker : public EventBroker <T, Q>
{
public:
    ESPEventBroker(size_t queue_size, size_t n_events):EventBroker<T, Q>(queue_size, n_events){}
    virtual ~ESPEventBroker(){}
    class Producer : public EventBroker<T, Q>::Producer
    {
    public:
        virtual void begin() override;
    };
    class Consumer : public EventBroker<T, Q>::Consumer
    {
    public:
        virtual void begin() override;
    };
};

//==============================================================================
// EspEventBroker implementation

//------------------------------------------------------------------------------
/// @brief producer thread loop
/// @details calls the send() function in a loop until done is set to true. 
/// The producer thread is pinned to core 1 and has the name "BrokerProducer".
template <typename T, typename Q> 
void ESPEventBroker<T, Q>::Producer::begin() {
    auto cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = 1; 
    cfg.thread_name = "BrokerProducer";
    esp_pthread_set_cfg(&cfg);

    this->producer_thread = std::thread([this](){loop();});
}

//------------------------------------------------------------------------------
/// @brief consumer thread loop
/// @details Consumer thread is pinned to core 0 and has the name "BrokerConsumer".
template <typename T, typename Q> 
void ESPEventBroker<T, Q>::Consumer::begin() {
    auto cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = 0; 
    cfg.thread_name = "BrokerConsumer";
    esp_pthread_set_cfg(&cfg);

    this->consumer_thread = std::thread([this](){loop();});
}

//= End of esp_event_broker.h ==================================================

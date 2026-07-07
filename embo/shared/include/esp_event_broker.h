#pragma once
//==============================================================================
// esp_event_broker.h
//------------------------------------------------------------------------------
///@brief Event broker optimized for ESP platform
//------------------------------------------------------------------------------
#include <esp_pthread.h>
#include <esp_cpu.h>
#include <threading.h>
#include "esp_queue.h"
#include "event_broker.h"

//==============================================================================
// class ESPEventBroker
template <typename T, typename Q = ESPCirQueue<T>> 
class ESPEventBroker : public EventBroker <T, Q>
{
    friend class ESPEventBroker::Producer;
    friend class ESPEventBroker::Consumer;
public:
    class Producer;
    class Consumer;
    ESPEventBroker(size_t queue_size, size_t n_events):
        EventBroker<T, Q>(queue_size, n_events, 
        new ESPEventBroker<T,Q>::Producer(this), 
        new ESPEventBroker<T,Q>::Consumer(this)){}
    virtual ~ESPEventBroker(){}
    virtual void begin()override;

    class Producer : public EventBroker<T, Q>::Producer
    {
    public:
        Producer(ESPEventBroker* broker) : 
            EventBroker<T,Q>::Producer(broker) {}
        virtual void begin() override;
    };
    class Consumer : public EventBroker<T, Q>::Consumer
    {
    public:
        Consumer(ESPEventBroker* broker) : EventBroker<T,Q>::Consumer(broker) {}
        virtual void begin() override;
    protected:
        virtual void loop() override;
    };

protected:
    //ESPEventBroker* broker;
};

//==============================================================================
// EspEventBroker implementation
//------------------------------------------------------------------------------
/// @brief ESPEventBroker begin
/// @details init semaphore and call the EventBroker begin to start threads
template <typename T, typename Q> 
void ESPEventBroker<T, Q>::begin() {
    EventBroker<T,Q>::begin();
    //this->semaphore.init();
}


//------------------------------------------------------------------------------
/// @brief producer thread loop
/// @details calls the send() function in a loop until done is set to true. 
/// The producer thread is pinned to core 1 and has the name "BrokerProducer".
template <typename T, typename Q> 
void ESPEventBroker<T, Q>::Producer::begin() {
    auto cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = 1; 
    cfg.thread_name = "BrokerProducer";
    cfg.prio = 1;
    esp_pthread_set_cfg(&cfg);

    this->producer_thread = std::thread([this](){this->loop();});
}

//------------------------------------------------------------------------------
/// @brief consumer thread loop
/// @details Consumer thread is pinned to core 0 and has the name "BrokerConsumer".
template <typename T, typename Q> 
void ESPEventBroker<T, Q>::Consumer::begin() {
    auto cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = 0; 
    cfg.thread_name = "BrokerConsumer";
    cfg.stack_size = 8192;
    esp_pthread_set_cfg(&cfg);

    this->consumer_thread = std::thread([this](){this->loop();});
}

template <typename T, typename Q> 
void ESPEventBroker<T, Q>::Consumer::loop() {
    bool done = false;
    if(!this->broker) {
        return;
    }
    while(!done) {
        ESPEventBroker* broker = (ESPEventBroker*)(this->broker);
        bool result = broker->semaphore.acquire();
        while(!broker->is_empty()) {
            T event;
            broker->pop_head(event);
            if(event.id == Event::KILL) {
                done = true;
            }
            else  {
                ESPEventBroker* broker = (ESPEventBroker*)this->broker;
                broker->roster.notify(event);
            } 
        }
        std::this_thread::yield();
    }
}

//= End of esp_event_broker.h ==================================================

//==============================================================================
// event_broker.h
/// @brief Event broker template class. Manages event publication and subscription,
/// and provides producer and consumer threads for event generation and processing.
//------------------------------------------------------------------------------
#pragma once
//- stdlib ---------------------------------------------------------------------
#include <thread>
#include <list>

//- local ----------------------------------------------------------------------
#include "queue.h"

template <typename T, typename Q> 
class EventBroker;
class Event;

class Event
{
public:
    enum {
        KILL,           // Kills the producer so consumers are safe to exit
        RESERVED = 9,   // Reserved events
        FIRST,          // Start enum from Event::FIRST in inherited classes
        LAST
    };
    Event(unsigned id = 0):id(id){}
    unsigned get_id() const {return id;}

 public:
    unsigned id;
};

//==============================================================================
// Class Eventbroker
/// @brief Event broker template class. Manages event publication and subscription,
/// and provides producer and consumer threads for event generation and processing.
/// @tparam T - event type, must inherit from Event
/// @tparam Q - queue type
template <typename T, typename Q = CirQueue<T>> 
class EventBroker : public Q
{
public:
    class Subscriber;

    EventBroker(size_t queue_size, size_t n_events);
    virtual ~EventBroker();
    bool publish(unsigned event_id);
    bool subscribe(unsigned event_id, EventBroker::Subscriber* subscriber);
    virtual void begin(){
        consumer->begin();
        producer->begin();
    };
    virtual void end() {
        done = true;
        producer->end();
        consumer->end();
    }
    virtual void send() = 0;

    //------------------------------------------------------------------------------
    /// @brief roster entry for each event type, containing the list of 
    /// subscribers and the event id (internal use only)
    class RosterEntry {
    public:
        RosterEntry():id(0){};
        void set_id(unsigned event_id) {id = event_id;}
        unsigned get_id() const {return id;}
        bool is_published() {return id != 0;}
        bool subscribe(unsigned event_id, Subscriber* subscriber);
        bool notify(T& event);

    private:
        std::list<Subscriber*> subscribers;
        unsigned id;
    };

    //------------------------------------------------------------------------------
    /// @brief producer thread class (internal use only)
    class Producer 
    {
    public:
        friend class EventBroker;
        Producer(EventBroker* broker):broker(broker) {}
        void begin();
        void end();
    
    private:
        void loop();
        EventBroker* broker;
        std::thread producer_thread;
    };

    //------------------------------------------------------------------------------
    /// @brief consumer thread class (internal use only)
    class Consumer
    {
    public:
        friend class EventBroker;
        Consumer(EventBroker* broker):broker(broker) {}
        void begin();
        void end();
        void loop();

    private:
        EventBroker* broker;
        std::thread consumer_thread;
    };

    //------------------------------------------------------------------------------
    /// @brief subscriber class. Users should inherit from this class and implement 
    /// the receive() function to define the behavior when an event is received. 
    /// The receive() function will be called in the consumer
    class Subscriber
    {
    public:
        Subscriber():active(true){}
        void set_active(bool active_ = true){active = active_;}
        bool is_active() const {return active;}
        virtual bool receive(const T& event) = 0;
    private:
        std::atomic<bool> active;
    };

private:
    const size_t n_events;
    Producer* producer;
    Consumer* consumer;
    RosterEntry* roster;
    std::atomic<bool> done;
};

//------------------------------------------------------------------------------
// EventBroker implementation

//------------------------------------------------------------------------------
/// @brief EventBroker constructor
/// @param queue_size - size of the event queue
/// @param n_events - number of different events that can be published
template <typename T, typename Q> 
EventBroker<T, Q>::EventBroker(size_t queue_size, size_t n_events):
Q(queue_size), n_events(n_events), done(false) {
    producer = new EventBroker::Producer(this);
    consumer = new EventBroker::Consumer(this);
    roster = new RosterEntry[n_events];
}

//------------------------------------------------------------------------------
/// @brief EventBroker destructor
template <typename T, typename Q> 
EventBroker<T, Q>::~EventBroker(){
    delete[] roster;
    delete producer;
    delete consumer;
}

//------------------------------------------------------------------------------
/// @brief publish event 
/// @param event_id - id of the event to publish
/// @return true if event was published successfully, false if event_id is out 
/// of range or event was already published
/// @details event_id must be in range [Event::FIRST, n_events) and must not be
/// already published. Once published, event_id cannot be published again.
template <typename T, typename Q> 
bool EventBroker<T, Q>::publish(unsigned event_id) {
    if (event_id < Event::FIRST || event_id >= n_events) {
        // Wrong id
        return false;
    }
    else if(roster[event_id].is_published()) {
        // Already published
        return false;
    }
    else {
        roster[event_id].set_id(event_id);
    }
    return true;
}

//------------------------------------------------------------------------------
/// @brief subscribe to event
/// @param event_id - id of the event to subscribe to
/// @param subscriber - pointer to the subscriber object
/// @return true if subscription was successful, false if event_id is out of
/// range or event was not published yet
/// @details event_id must be in range [Event::FIRST, n_events) and must be
/// already published. Multiple subscribers can subscribe to the same event_id.
template <typename T, typename Q> 
bool EventBroker<T, Q>::subscribe(unsigned event_id, EventBroker::Subscriber* subscriber) {
    if (event_id >= Event::FIRST && event_id < n_events) {
        return roster[event_id].subscribe(event_id, subscriber);
    } else {
        return false;
    }
}

//------------------------------------------------------------------------------
/// @brief notify subscribers of the event (internal use only)
/// @param event - event to notify about
/// @return true if event was published and subscribers were notified, false if
/// event_id is out of range or event was not published yet
template <typename T, typename Q> 
bool EventBroker<T, Q>::RosterEntry::subscribe(unsigned event_id, EventBroker::Subscriber* subscriber) {
    if(event_id == id) {
        subscribers.push_back(subscriber);
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
/// @brief notify subscribers of the event (internal use only)
/// @param event - event to notify about
/// @return true if event was published and subscribers were notified, false if
/// event_id is out of range or event was not published yet
template <typename T, typename Q> 
bool EventBroker<T, Q>::RosterEntry::notify(T& event) {
    if(event.get_id() == id) {
        for(auto s:subscribers) {
            if(s->is_active()) {
                s->receive(event);
            }
        }
        return true;
    }
    return false;
};

//------------------------------------------------------------------------------
/// @brief begin producer thread
/// @details starts the producer thread which will call send() in a loop until
/// end() is called. send() is a pure virtual function that must be implemented
/// in the derived class to define the behavior of the producer thread.
template <typename T, typename Q> 
void EventBroker<T, Q>::Producer::begin() {
    producer_thread = std::thread([this](){loop();});
}

//------------------------------------------------------------------------------
/// @brief end producer thread
/// @details signals the producer thread to stop by setting done to true and
/// waits for the thread to finish. The producer thread will check the done flag
/// in each iteration of the loop and exit when it is set to true.
template <typename T, typename Q> 
void EventBroker<T, Q>::Producer::end() {
    // Double check it isn't hanging in a joinable state
    broker->done.store(true, std::memory_order_relaxed);
    if (producer_thread.joinable()) {
        producer_thread.join(); 
    }
}

//------------------------------------------------------------------------------
/// @brief producer thread loop
/// @details calls the send() function in a loop until done is set to true. 
template <typename T, typename Q> 
void EventBroker<T, Q>::Producer::loop() {
    while(!broker->done.load(std::memory_order_relaxed)) {
        broker->send();
    }
    // This will be the last event in the queue
    T* event_kill = broker->allocate();
    if(event_kill) {
        event_kill->id = Event::KILL;
        broker->push_tail();
    }
}

//------------------------------------------------------------------------------
/// @brief begin consumer thread
/// @details starts the consumer thread which will wait for events to be published
/// and notify subscribers until it receives the KILL event. The consumer thread
/// will check the queue for new events and call the notify() function for each
/// event until it receives the KILL event, at which point it will exit.
template <typename T, typename Q> 
void EventBroker<T, Q>::Consumer::begin() {
    consumer_thread = std::thread([this](){loop();});
}


//------------------------------------------------------------------------------
/// @brief end consumer thread
/// @details waits for the consumer thread to finish. The consumer thread will
/// exit when it receives the KILL event, which is sent by the producer thread
/// when end() is called. This function should be called after end() is called on
/// the producer to ensure that the KILL event is sent and the consumer thread can 
/// exit gracefully.
template <typename T, typename Q> 
void EventBroker<T, Q>::Consumer::end() {
    // Double check it isn't hanging in a joinable state
    if (consumer_thread.joinable()) {
        consumer_thread.join(); 
    }
}

//------------------------------------------------------------------------------
/// @brief consumer thread loop
/// @details waits for events to be published and notifies subscribers until it
/// receives the KILL event. The consumer thread will check the queue for new
/// events and call the notify() function for each event until it receives the
/// KILL event, at which point it will exit.
template <typename T, typename Q> 
void EventBroker<T, Q>::Consumer::loop() {
    bool done = false;
    while(!done) {
        if(!broker->is_empty()) {
            T event;
            broker->pop_head(event);
            if(event.id == Event::KILL) {
                done = true;
            }
            else  {
                broker->roster[event.id].notify(event);
            } 
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
//- End of event_broker.h ======================================================
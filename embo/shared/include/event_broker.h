//==============================================================================
// event_broker.h
/// @brief Event broker template class. Manages event publication and subscription,
/// and provides producer and consumer threads for event generation and processing.
//------------------------------------------------------------------------------
#pragma once
#include <Arduino.h> // ToDo: delete after debugging
//- stdlib ---------------------------------------------------------------------
#include <thread>
#include <list>
#include <vector>
#include <tuple>
#include <functional>

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
        FIRST           // Start enum from Event::FIRST in inherited classes
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
    class RosterEntry;
    friend class Roster;
    class Subscription;
    class Subscriber;
    class Producer;
    class Consumer;
    using EventCallback = std::function<void(const Subscription&, const T&)>;

    EventBroker(size_t queue_size, size_t n_events, Producer* producer = nullptr, Consumer* consumer = nullptr);
    virtual ~EventBroker();
    bool publish(unsigned event_id);
    bool subscribe(const Subscription& subscription);
    unsigned index(unsigned event_id) const {return event_id - Event::FIRST;}
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
    virtual void receive(const Subscription& subscription, T& event) {
        Subscriber* subscriber = subscription.get_subscriber();
        EventCallback callback = subscription.get_callback();
        if(subscriber->is_active()) {
            callback(subscription, event);
        }
    }

    //------------------------------------------------------------------------------
    /// @brief roster entry for each event type, containing the list of 
    /// subscribers and the event id (internal use only)
    class RosterEntry 
    {
    public:
        friend class EventBroker::Roster;

        RosterEntry():id(0){};
        void set_id(unsigned event_id) {id = event_id;}
        unsigned get_id() const {return id;}
        bool is_published() {return id != 0;}
        bool subscribe(const Subscription& subscription);

    private:
        std::list<Subscription> subscriptions;
        unsigned id;
    };

    class Roster : public std::vector<RosterEntry> 
    {
    public:
        Roster(EventBroker* broker):broker(broker) {
            this->reserve(broker->n_events);
            for(int i = 0; i < broker->n_events; i++) {
                this->emplace_back();
            }
        };

        EventBroker* get_broker() {return broker;}

        virtual bool notify(T& event) {
            RosterEntry& entry = this->at(broker->index(event.id));
            if(event.get_id() == entry.get_id()) {
                for(auto s:entry.subscriptions) {
                    if(s.get_subscriber()->is_active()) {
                        broker->receive(s, event);
                    }
                }
                return true;
            }
            return false;
        }

    protected:
        EventBroker* broker;
    };

    //------------------------------------------------------------------------------
    /// @brief producer thread class (internal use only)
    class Producer 
    {
    public:
        friend class EventBroker;
        Producer(EventBroker* broker):broker(broker) {}
        virtual void begin();
        virtual void end();
    
    protected:
        virtual void loop();
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
        virtual void begin();
        virtual void end();

    protected:
        virtual void loop();
        EventBroker* broker;
        std::thread consumer_thread;
    };

  
    //------------------------------------------------------------------------------
    /// @brief subscriber class. Users should inherit from this class and implement 
    /// the receive() function to define the behavior when an event is received. 
    /// The receive() function will be called in the consumer
    class Subscriber
    {
        friend class RosterEntry;
    public:
        Subscriber(EventBroker* broker = nullptr):broker(broker){}
        void set_active(bool active_ = true){active = active_;}
        bool is_active() const {return active;}
        /*
        void set_callback(EventCallback callback) {this->callback = callback;}
        virtual void receive(const T& event) {
            if (callback) callback(event);
        }
        EventCallback callback;
        */
        bool subscribe(const Subscription& subscription) {
            if(broker) {
                return broker->subscribe(subscription);
            }
            return false;
        }
    protected:
        EventBroker* broker;
        std::atomic<bool> active;
    };

    //------------------------------------------------------------------------------
    class Subscription {
    public:
        Subscription(unsigned event_id, Subscriber* subscriber, unsigned flags,  EventCallback callback) : 
        event_id(event_id), subscriber(subscriber), flags(flags), callback(callback){}
        unsigned get_event_id() const {return event_id;}
        Subscriber* get_subscriber() const {return subscriber;}
        void set_subscriber(Subscriber* subscriber) {this->subscriber = subscriber;}
        EventCallback get_callback() const {return callback;}
        unsigned get_flags() const {return flags;}

    private:
        unsigned event_id;
        Subscriber* subscriber;
        EventCallback callback;
        unsigned flags;
    };

protected:
    const size_t n_events;
    Producer* producer;
    Consumer* consumer;
    Roster roster;
    std::atomic<bool> done;
};

//------------------------------------------------------------------------------
// EventBroker implementation

//------------------------------------------------------------------------------
/// @brief EventBroker constructor
/// @param queue_size - size of the event queue
/// @param n_events - number of different events that can be published
template <typename T, typename Q> 
EventBroker<T, Q>::EventBroker(size_t queue_size, size_t n_events, Producer* producer, Consumer* consumer) :
Q(queue_size), n_events(n_events), roster(this), done(false) {
    this->producer = producer ? producer : new Producer(this);
    this->consumer = consumer ? consumer : new Consumer(this);
    roster.reserve(n_events);
}

//------------------------------------------------------------------------------
/// @brief EventBroker destructor
template <typename T, typename Q> 
EventBroker<T, Q>::~EventBroker(){
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
    if (index(event_id) >= n_events) {
        // Wrong id
        return false;
    }
    else if(roster[index(event_id)].is_published()) {
        // Already published
        return false;
    }
    else {
        roster[index(event_id)].set_id(event_id);
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
bool EventBroker<T, Q>::subscribe(const Subscription& subscription) {
    unsigned event_id = subscription.get_event_id();
    if (event_id >= Event::FIRST && event_id < (Event::FIRST + n_events)) {
        return roster[index(event_id)].subscribe(subscription);
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
bool EventBroker<T, Q>::RosterEntry::subscribe(const Subscription& subscription) {
    
    if(subscription.get_event_id() == id) {
        subscriptions.push_back(subscription);
        return true;
    }
    return false;
}

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
    if(!broker) {
        return;
    }
    while(!broker->done.load(std::memory_order_relaxed)) {
        broker->send();
        std::this_thread::yield();
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
                // broker->roster[broker->index(event.id)].notify(event);
                broker->roster.notify(event);
            } 
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::this_thread::yield();
        }
    }
}
//- End of event_broker.h ======================================================
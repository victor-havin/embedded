#include <Arduino.h>
#include <threading.h>
#include <esp_event_broker.h>

using Broker = ESPEventBroker<Event>;
using Subscriber = ESPEventBroker<Event>::Subscriber;
using Subscription = ESPEventBroker<Event>::Subscription;

static void print_thread(Broker* broker) {

    TaskHandle_t this_handle = xTaskGetCurrentTaskHandle();
    pthread_t this_id;
    memcpy(&this_id, &this_handle, sizeof(pthread_t));
    pthread_t consumer_id = broker->get_consumer_id();
    pthread_t producer_id = broker->get_producer_id();
    if(this_id == consumer_id) {
        Serial.println("Consumer Thread");
    } else if (this_id == producer_id) {
        Serial.println("Producer Thread");
    } else {
        Serial.println("Some other thread");
    }
}


class MyEvent : public Event {
public:
    enum {
        EVENT_1 = Event::FIRST
    };
    MyEvent(unsigned event_id) : Event(event_id) {}
};

class MyBroker : public Broker {
public:
    MyBroker();
    virtual void send() override;
    virtual void receive(const Subscription& subscription, Event& event) override;
    Subscriber subscriber;
};

MyBroker::MyBroker() : Broker(128, 1), subscriber(this) {

}

static int last = 0;
void MyBroker::send() {
    Event* e = allocate();
    e->id = MyEvent::EVENT_1;
    push_tail();
    print_thread(this);
    vTaskDelay(pdMS_TO_TICKS(1000));  
    std::this_thread::yield(); // Yield to allow other threads to run
}

void MyBroker::receive(const Subscription& subscription, Event& event) {
    Event e;
    if(pop_head(e)) {
        if(event.get_id() == MyEvent::EVENT_1) {
            print_thread(this);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));  
    std::this_thread::yield(); // Yield to allow other threads to run
}


void setup() {
    Serial.begin(115200);
    MyBroker* broker = new MyBroker();
    delay(2000);
    broker->publish(MyEvent::EVENT_1);
    broker->subscribe(Subscription(MyEvent::EVENT_1, &broker->subscriber, 0, nullptr));
    broker->begin();
    print_thread(broker);
    while(true) delay(1000);
    broker->end();
    delete broker;
}

void loop() {

}
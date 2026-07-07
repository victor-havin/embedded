//==============================================================================
// ui_composer.cpp
// UIComposer class implementation
//==============================================================================
#include <threading.h>
#include <ui_composer.h>

UIComposer* UIComposer::this_instance = nullptr;

//------------------------------------------------------------------------------
UIComposer* UIComposer::factory(LGFX_Device* display, size_t queue_size) {
    this_instance = new UIComposer(display, queue_size);
    for(unsigned ev = UIEvent::EVT_FIRST; ev < UIEvent::EVT_LAST; ev++) {
        this_instance->publish(ev);
    }
    return this_instance;
}

//------------------------------------------------------------------------------
UIComposer::~UIComposer() {
    for(auto zone:zones) {
        if(zone) delete zone;
    }
}

//------------------------------------------------------------------------------
void UIComposer::begin() {
    if(display->touch() != nullptr) {
        auto config = display->touch()->config();
        pin_int = config.pin_int;
        semaphore.init();
        // Trigger ISR on touch.
        pinMode(pin_int, INPUT_PULLUP);
        attachInterrupt(pin_int, this_instance->touch_isr, FALLING); 
    }

    render();
    ESPEventBroker::begin();
    invalidate();
}

//------------------------------------------------------------------------------
void UIComposer::end() {
    detachInterrupt(pin_int);
    semaphore.release();
    ESPEventBroker::end();
}

//------------------------------------------------------------------------------
void UIComposer::invalidate() {
    for(auto zone:zones) {
        zone->invalidate();
    }
}

//------------------------------------------------------------------------------
void UIComposer::render() {
    Subscription subscription(UIEvent::EVT_UPDATE, &subscriber, 0, [this](const Subscription& s, const UIEvent& event) {
        Component* component = event.get_source();
        component->update();
    });
    this->subscribe(subscription);
    for(auto zone:zones) {
        zone->render();
    }
}

//------------------------------------------------------------------------------
// UIComposer::send()
// Called by the producer thread to generate events.
// For example, it generates events based on touch screen interactions.
//
void UIComposer::send() {
    static int touch_x = 0, touch_y = 0;
    static int last_x = 0, last_y = 0;
    static bool is_tracking_touch = false;

    // 1. Passive Mode: Sleep indefinitely until an ISR or an external UI event wakes us up
    if (!is_tracking_touch) {
        bool result = semaphore.acquire();
    } else {
        // 2. High-Speed Drag Mode: Force a precise 20ms frame-rate constraint
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // ==========================================================
    // SOURCE 1: External Queue Events
    // Check if an external thread gave the semaphore while we were busy
    // ==========================================================
    UIEvent inbound_event;
    while (system_queue.pop_head(inbound_event)) {
        // Serial.printf("UIComposer::event() processing inbound event id: %d\n", inbound_event.get_id());
        UIEvent* event_slot = allocate();
        if (event_slot) {
            *event_slot = inbound_event;
            push_tail();
        }
    }

    while (user_queue.pop_head(inbound_event)) {
        // Serial.printf("UIComposer::event() processing inbound event id: %d\n", inbound_event.get_id());
        UIEvent* event_slot = allocate();
        if (event_slot) {
            *event_slot = inbound_event;
            push_tail();
        }
    }

    // Consume the semaphore latch left behind by the external thread's release()
    bool result =semaphore.try_acquire(); 

    //Serial.printf("UIComposer::event() entered. Queue length: %zu\n", get_length());

    // ==========================================================
    // SOURCE 2: Hardware Touch Engine (Tight State Machine)
    // ==========================================================
    // Read the physical pin state at this exact microsecond
    bool current_pin_low = (digitalRead(pin_int) == LOW);
    bool touch = false;
    int retry = 0;
    display->waitDMA();
    // Condition A: Initial Press
    if (current_pin_low && !is_tracking_touch) {
        while(!touch && retry < 10) {
            touch = display->getTouch(&touch_x, &touch_y);
            vTaskDelay(pdMS_TO_TICKS(5)); // Wait 5ms before retrying
            retry++;
        }
        if (touch) {
            is_tracking_touch = true;
            last_x = touch_x;
            last_y = touch_y;
            UIEvent* event_touch = allocate();
            if (event_touch) {
                Variant var(Variant::Point({touch_x, touch_y}));
                event_touch->set_variant(var);
                event_touch->id = UIEvent::EVT_TOUCH_DOWN;
                Serial.printf("Touch down event at (%d, %d)\n", touch_x, touch_y);
                push_tail();
            }
        }
    } 
    // Condition B: Continuous High-Speed Drag
    else if (current_pin_low && is_tracking_touch) {
        if (display->getTouch(&touch_x, &touch_y)) {
            if (abs(touch_x - last_x) > 5 || abs(touch_y - last_y) > 5) { // Only trigger if position has changed significantly
                last_x = touch_x;
                last_y = touch_y;

                UIEvent* event_touch = allocate();
                if (event_touch) {
                    Variant var(Variant::Point({touch_x, touch_y}));
                    event_touch->set_variant(var);
                    event_touch->id = UIEvent::EVT_TOUCH_DRAG;
                    Serial.printf("Touch drag event at (%d, %d)\n", touch_x, touch_y);
                    push_tail();
                }
            }
        }
    }
    // Condition C: Physical Finger Release
    else if (!current_pin_low && is_tracking_touch) {
        UIEvent* event_touch = allocate();
        if (event_touch) {
            Variant var(Variant::Point({last_x, last_y}));
            event_touch->set_variant(var);
            event_touch->id = UIEvent::EVT_TOUCH_UP;
            Serial.printf("Touch up event at (%d, %d)\n", last_x, last_y);
            push_tail();
        }
        is_tracking_touch = false; // Drops the clutch back into passive sleep mode
        
        // Clear any stale interrupt noise generated during the physical finger lift
        bool result = semaphore.try_acquire(); 
    }
}


//------------------------------------------------------------------------------
bool UIComposer::event(UIEvent& event) {
    // Push the event to the inbound queue for thread-safe processing
    bool result = user_queue.push_tail(event);
    if(result) {
        // Signal that a UI event is available
        //Serial.printf("UIComposer::event() pushed event id: %d to inbound queue\n", event.get_id());
        semaphore.release();
    }
    return result;
}

//------------------------------------------------------------------------------
bool UIComposer::system_event(UIEvent& event) {
    bool result = system_queue.push_tail(event);
    if(result) {
        // Signal that a UI event is available
        //Serial.printf("UIComposer::event() pushed event id: %d to inbound queue\n", event.get_id());
        semaphore.release();
    }
    return result;
}

//------------------------------------------------------------------------------
void UIComposer::receive(const Subscription& subscription,  UIEvent& event) {
    //Serial.printf("UIComposer::receive() called with event id: %d\n", event.get_id());
    max_queue_len = std::max(max_queue_len, get_length());
    switch(event.id) {
        case UIEvent::EVT_TOUCH_DOWN:
        case UIEvent::EVT_TOUCH_UP:
        case UIEvent::EVT_TOUCH_DRAG:
        {
            Component* component = nullptr;
            Zone* zone = find_zone(event.get_variant().get_point());
            if(zone) {
                Variant::Point ref = zone->get_position().p;
                zone->notify_hit(subscription, event, ref);
            }
            break;
        }
        default:
            auto callback = subscription.get_callback();
            if(subscription.get_flags() & UIComposer::SUB_SELF) {
                if(subscription.get_subscriber() == event.get_source()) {
                    callback(subscription, event);
                }
            } else {
                callback(subscription, event);
            }
            break;
    }
}

//==============================================================================
// Private methods

//------------------------------------------------------------------------------
void UIComposer::touch_isr() {
    static volatile uint32_t last_interrupt_time = 0;
    // Debounce repeating events
    if (micros() - last_interrupt_time < 15000) { 
        return; // Exit immediately in 2 clock cycles
    }
    last_interrupt_time = micros();
    // Releas the producer thread semaphore
    this_instance->semaphore.release_from_isr();
}

//= End of ui_composer.cpp =====================================================


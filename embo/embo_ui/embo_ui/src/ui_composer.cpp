//==============================================================================
// ui_composer.cpp
// UIComposer class implementation
//==============================================================================
#include <threading.h>
#include <ui_composer.h>
#include <shared.h>

UIComposer* UIComposer::this_instance = nullptr;

const Transition UIComposer::StateTable[4][8] = {
    // -------------------------------------------------------------------------
    // CURRENT STATE: Idle (0)
    // -------------------------------------------------------------------------
    {
        { Component::State::IDLE,    nullptr },               // 000: Up->Up, Out (NR)
        { Component::State::IDLE,    nullptr },               // 001: Up->Up, In (NR)
        { Component::State::IDLE,    nullptr },               // 010: Up->Down, Out
        { Component::State::PRESSED, &UIComposer::do_press }, // 011: Up->Down, In (Press!)
        { Component::State::IDLE,    nullptr },               // 100: Down->Up, Out
        { Component::State::IDLE,    nullptr},                // 101: Down->Up, In (NR)
        { Component::State::IDLE,    nullptr },               // 110: Down->Down, Out
        { Component::State::IDLE,    nullptr }                // 111: Down->Down, In (NR)
    },
    // -------------------------------------------------------------------------
    // CURRENT STATE: Dragged (1)
    // -------------------------------------------------------------------------
    {
        { Component::State::IDLE,    nullptr },                 // 000: Up->Up, Out (NR)
        { Component::State::IDLE,    nullptr },                 // 001: Up->Up, In (NR)
        { Component::State::IDLE,    nullptr },                 // 010: Up->Down, Out (NR)
        { Component::State::IDLE,    &UIComposer::do_press},    // 011: Up->Down, In  ? Missed transition ?
        { Component::State::SLIDOUT, nullptr},                  // 100: Down->Up, Out
        { Component::State::IDLE,    &UIComposer::do_release},  // 101: Down->Up, In
        { Component::State::IDLE,    &UIComposer::do_leave },   // 110: Down->Down, Out
        { Component::State::DRAGGED, nullptr }                  // 111: Down->Down, In
    },
    // -------------------------------------------------------------------------
    // CURRENT STATE: Pressed (2)
    // -------------------------------------------------------------------------
    {
        { Component::State::IDLE,    nullptr },                 // 000: Up->Up, Out (NR)
        { Component::State::IDLE,    nullptr },                 // 001: Up->Up, In (NR)
        { Component::State::IDLE,    nullptr },                 // 010: Up->Down, Out (NR)
        { Component::State::IDLE,    nullptr },                 // 011: Up->Down, In (NR)
        { Component::State::IDLE,    nullptr },                 // 100: Down->Up, Out
        { Component::State::IDLE,    &UIComposer::do_release }, // 101: Down->Up, In (Click Release!)
        { Component::State::SLIDOUT, &UIComposer::do_leave },   // 110: Down->Down, Out (Slide-out)
        { Component::State::DRAGGED, nullptr }                  // 111: Down->Down, In
    },
    // -------------------------------------------------------------------------
    // CURRENT STATE: SlidOut (3)
    // -------------------------------------------------------------------------
    {
        { Component::State::IDLE,    nullptr },               // 000: Up->Up, Out
        { Component::State::IDLE,    nullptr },               // 001: Up->Up, In
        { Component::State::IDLE,    nullptr },               // 010: Up->Down, Out
        { Component::State::IDLE,    nullptr },               // 011: Up->Down, In
        { Component::State::IDLE,    nullptr },               // 100: Down->Up, Out
        { Component::State::IDLE,    &UIComposer::do_release},// 101: Down->Up, In (Slide-back-in)
        { Component::State::SLIDOUT, nullptr },               // 110: Down->Down, Out
        { Component::State::DRAGGED, &UIComposer::do_enter }  // 111: Down->Down, In (Slide-back-in)
    }
};

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
    init();
    invalidate();
}

//------------------------------------------------------------------------------
void UIComposer::end() {
    detachInterrupt(pin_int);
    semaphore.release();
    ESPEventBroker::end();
}

//------------------------------------------------------------------------------
void UIComposer::init()
{
    UIEvent event(UIEvent::EVT_INIT, nullptr);
    this->event(event);
    for(auto zone:zones) {
        UIEvent event(UIEvent::EVT_INIT, zone);
        this->event(event);
    }
}

//------------------------------------------------------------------------------
void UIComposer::invalidate() {
    for(auto zone:zones) {
        zone->invalidate();
    }
}

//------------------------------------------------------------------------------
void UIComposer::render() {
    // This invokes initialization along the component tree at start.
    this->subscribe(Subscription(
        UIEvent::EVT_INIT, &subscriber, 0, 
        [this](const Subscription& s, const UIEvent& event) {
            if(event.get_source()) {
                Component* component = event.get_source();
                component->init();
            } else {
                image_registry.init();
            }

    }));
    // This the update central. It saves components from
    // subscribing to individual updates
    this->subscribe(Subscription(
        UIEvent::EVT_UPDATE, &subscriber, 0, 
        [](const Subscription& s, const UIEvent& event) {
            Component* component = event.get_source();
            component->update();
        }));
    
    // Below are subscriptions to touch events.
    // This makes broker invoke the consumer receive() for
    // the initial processing.
    this->subscribe(Subscription(
        UIEvent::EVT_TOUCH_DOWN, &subscriber, 0, 
        [this](const Subscription& s, const UIEvent& event) {
            Component* component = event.get_source();
    }));
    this->subscribe(Subscription(
        UIEvent::EVT_TOUCH_UP, &subscriber, 0, 
        [this](const Subscription& s, const UIEvent& event) {
            Component* component = event.get_source();
    }));
    this->subscribe(Subscription(
        UIEvent::EVT_TOUCH_DRAG, &subscriber, 0, 
        [this](const Subscription& s, const UIEvent& event) {
            Component* component = event.get_source();
    }));
    
    // Render everything and gater the flat component list for the state machine
    for(auto zone:zones) {
        zone->render();
        zone->gather(components);
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
#ifdef DEBUG_PRINT                
                Serial.printf("Touch down event at (%d, %d)\n", touch_x, touch_y);
#endif
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
#ifdef DEBUG_PRINT
                    Serial.printf("Touch drag event at (%d, %d)\n", touch_x, touch_y);
#endif
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
#ifdef DEBUG_PRINT
            Serial.printf("Touch up event at (%d, %d)\n", last_x, last_y);
#endif
            push_tail();
        }
        is_tracking_touch = false; // Drops the clutch back into passive sleep mode
        
        // Clear any stale interrupt noise generated during the physical finger lift
        bool result = semaphore.try_acquire(); 
    }
}

//------------------------------------------------------------------------------
void UIComposer::add_timer(Subscription& subscription, unsigned interval) {
    subscription.set_aux((void*)interval);
    subscribe(subscription);
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
void UIComposer::receive(const Subscription& subscription, UIEvent& event) {
    max_queue_len = std::max(max_queue_len, get_length());

    switch(event.id) {
        // --- PHASE 1: Low-Level System Touches (Process State Matrix) ---
        case UIEvent::EVT_TOUCH_DOWN:
        case UIEvent::EVT_TOUCH_UP:
        case UIEvent::EVT_TOUCH_DRAG:
        {
            Variant::Point point = event.get_variant().get_point();
            // Map instantaneous hardware state flags
            bool is_down = (event.id == UIEvent::EVT_TOUCH_DOWN || event.id == UIEvent::EVT_TOUCH_DRAG);

            // Single linear pass over pre-flattened component list.
            for (Component* comp : components) {
                if (!comp->is_visible()) continue;

                bool is_inside = comp->is_within_abs(point);
                Component::State state = comp->get_state();
                bool was_down = (state != Component::State::IDLE);
                byte token = ((was_down ? 1 : 0) << 2) | ((is_down ? 1 : 0) << 1) | (is_inside ? 1 : 0);
                
#ifdef DEBUG_PRINT
                if(is_inside) {
                    Variant::Position pos = comp->get_position();
                    Serial.printf("Inside component {{%d,%d},{%d, %d}} state %d token %1x\n", 
                        pos.p.x, pos.p.y, pos.s.w, pos.s.h, state, token
                    );
                }
#endif
                Transition t = StateTable[static_cast<uint8_t>(state)][token];
                if (t.action != nullptr) {
                    (this->*(t.action))(comp, point);
                }
                comp->set_state(t.next_state);
            }
            [[fallthrough]];
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

//------------------------------------------------------------------------------
void UIComposer::do_press(Component* component, Variant::Point pt) {
#ifdef DEBUG_PRINT
    Variant::Position pos = component->get_position();
    Serial.printf("Action do_press on component at {{%d,%d},{%d, %d}}\n", 
        pos.p.x, pos.p.y, pos.s.h, pos.s.w
    );
#endif
    UIEvent event(UIEvent::EVT_PRESS, component);
    Variant var(pt);
    event.set_variant(var);
    system_event(event);
}

//------------------------------------------------------------------------------
void UIComposer::do_release(Component* component, Variant::Point pt) {
#ifdef DEBUG_PRINT
    Variant::Position pos = component->get_position();
    Serial.printf("Action do_release on component at {{%d,%d},{%d, %d}}\n", 
        pos.p.x, pos.p.y, pos.s.h, pos.s.w
    );
#endif
    UIEvent event(UIEvent::EVT_RELEASE, component);
    Variant var(pt);
    event.set_variant(var);
    system_event(event);
}

//------------------------------------------------------------------------------
void UIComposer::do_enter(Component* component, Variant::Point pt) {
#ifdef DEBUG_PRINT
    Variant::Position pos = component->get_position();
    Serial.printf("Action do_enter on component at {{%d,%d},{%d, %d}}\n", 
        pos.p.x, pos.p.y, pos.s.h, pos.s.w
    );
#endif
    UIEvent event(UIEvent::EVT_ENTER, component);
    Variant var(pt);
    event.set_variant(var);
    system_event(event);
}

//------------------------------------------------------------------------------
void UIComposer::do_leave(Component* component, Variant::Point pt) {
#ifdef DEBUG_PRINT
    Variant::Position pos = component->get_position();
    Serial.printf("Action do_leave on component at {{%d,%d},{%d, %d}}\n", 
        pos.p.x, pos.p.y, pos.s.h, pos.s.w
    );
#endif
    UIEvent event(UIEvent::EVT_LEAVE, component);
    Variant var(pt);
    event.set_variant(var);
    system_event(event);
}

//==============================================================================
// Class Image implementation

//------------------------------------------------------------------------------
Image::Image(const char* image_source) :
image_sprite(nullptr) {
    if(is_static_mem((void*)image_source)) {
        this->image_source = image_source;
        this->source_allocated = false;
    } else {
        this->image_source = strdup(image_source);
        this->source_allocated = true;
    }
}

//------------------------------------------------------------------------------
Image::~Image() {
    if(image_sprite) delete image_sprite;
    if(source_allocated && image_source) {
        free((void*)image_source);
    }
}

//==============================================================================
// Class ImageRegistry implementation

//------------------------------------------------------------------------------
void ImageRegistry::add(Image image, int image_id)  {
    if(image_container.size() <= image_id) image_container.resize(image_id + 1);
    image_container[image_id] = image;
};

//------------------------------------------------------------------------------
void ImageRegistry::add(const char * image_source, int image_id) {
    if(image_container.size() <= image_id) image_container.resize(image_id + 1);
    Image image(image_source);
    image_container[image_id] = image;
}

//------------------------------------------------------------------------------
void ImageRegistry::init() {
    for(auto& image: image_container) {
        load_from_fs(&image);
    }
}

//------------------------------------------------------------------------------
Image* ImageRegistry::get(int id) {
    if(id < 0 || id >= image_container.size()) return nullptr;
    return &image_container[id];
}

//------------------------------------------------------------------------------
bool ImageRegistry::load_from_fs(Image* image) {
   if(image->get_source() == nullptr || image->get_sprite() != nullptr) return false;
    fs::File file = LittleFS.open(image->get_source(), "r");
    if (!file) {
        Serial.printf("Error: Could not open icon file: %s\n", image->get_source());
        return 0;
    }

    uint8_t header[24];
    int img_w = 0;
    int img_h = 0;

    if (file.read(header, 24) == 24) {
        if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) {
            // FIX: Ensure you extract from the exact standard PNG IHDR locations
            // Bytes 16-19 for Width, Bytes 20-23 for Height
            img_w = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
            img_h = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];
        }
    }

    if (img_w <= 0 || img_h <= 0 || img_w > 480 || img_h > 480) { // Add safety thresholds
        Serial.printf("Error: Broken PNG dimensions calculated for %s\n", image->get_source());
        file.close();
        return 0;
    }

    LGFX_Sprite* sprite = new LGFX_Sprite(composer->get_display());
    sprite->setColorDepth(16);
    sprite->setPsram(true);
    
    // Check if allocation actually succeeds
    if (sprite->createSprite(img_w, img_h) == nullptr) {
        Serial.printf("Error: Failed to allocate RAM for sprite %dx%d\n", img_w, img_h);
        delete sprite;
        file.close();
        return false;
    }

    file.seek(0); 
    bool success = sprite->drawPng(&file, 0, 0);
    file.close(); 

    if (!success) {
        Serial.printf("Error: LovyanGFX decoder failed on file: %s\n", image->get_source());
        sprite->deleteSprite();
        delete sprite;
        return false;
    }
    Serial.printf("Image %s loaded.\n", image->get_source());
    image->set_sprite(sprite);
    return true;
}

//= End of ui_composer.cpp =====================================================


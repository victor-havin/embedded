//==============================================================================
// ui_event.h
//==============================================================================
#pragma once
#include <variant.h>

class Variant;
class Component;

class UIEvent : public Event 
{
public:
    enum UIEventID : uint16_t {
        EVT_FIRST = Event::FIRST,   // First past reserved for messge broker
        EVT_INIT,                   // Init. Called once at start.
        EVT_UPDATE,                 // Update. Caused by inavlidate()
        EVT_TOUCH_DOWN,             // Panel touch down
        EVT_TOUCH_UP,               // Panel touch up   
        EVT_TOUCH_DRAG,             // Panel touch drag
        EVT_PRESS,                  // Component pressed
        EVT_RELEASE,                // Component released
        EVT_ENTER,                  // Component entered
        EVT_LEAVE,                  // Component left
        EVT_INPUT,                  // Input generated
        EVT_VISIBLE,                // Component visibility changed
        EVT_TIMER,                  // Timer event
        EVT_SLEEP,                  // Sleep
        EVT_WAKE,                   // Wake up
        EVT_LAST                    // Guard
    };
    UIEvent() : Event(EVT_FIRST), millis(::millis()) {};
    UIEvent(UIEventID id, Component* source = nullptr) : 
        Event(id), source(source) {this->millis = ::millis();}
    int get_millis() const {return millis;}
    void set_variant(Variant& variant) {this->variant = variant;}
    const Variant& get_variant() const {return variant;}
    Component* get_source() const {return source;}
    void set_source(Component* source) {this->source = source;}

private:
    int millis = 0;
    Component* source = nullptr;
    Variant variant;
};
//= End of event_broker.h ======================================================
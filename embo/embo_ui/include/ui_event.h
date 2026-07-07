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
        EVT_FIRST = Event::FIRST,
        EVT_UPDATE,
        EVT_TOUCH_DOWN,
        EVT_TOUCH_UP,
        EVT_TOUCH_DRAG,
        EVT_PRESS,
        EVT_RELEASE,
        EVT_ENTER,
        EVT_LEAVE,
        EVT_INPUT,
        EVT_LAST
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
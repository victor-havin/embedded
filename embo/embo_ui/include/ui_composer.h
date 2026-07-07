//==============================================================================
// Composer.h
// UIComposer class definition
//==============================================================================
#pragma once
#include <vector>
#include <threading.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_event_broker.h>
#include <ui_event.h>
#include <ui_components.h>

class UIComposer;

//------------------------------------------------------------------------------
class UIComposer : public ESPEventBroker<UIEvent>
{
public:
    enum SubFlags : unsigned int{
        SUB_SELF = 1,           // Subscribe only to events from itself
        SUB_ALL = 0xFFFFFFFF
    };
    UIComposer(LGFX_Device* display, int queue_size = 512):
    ESPEventBroker<UIEvent>(queue_size, UIEvent::EVT_LAST - Event::FIRST),
    subscriber(this),
    display(display),
    system_queue(64),
    user_queue(64) {
        roster = this;
        zones.reserve(get_max_zones());
    }
    UIComposer(const UIComposer&) = delete;
    UIComposer& operator=(const UIComposer&) = delete;
    virtual ~UIComposer();
    
    static UIComposer* factory(LGFX_Device* display, size_t queue_size = 512);
    static UIComposer* get_instance() {return this_instance;}
    UIComposer::Subscriber* get_subscriber() {return &subscriber;}
    virtual void begin() override;
    virtual void end() override;
    virtual void send() override;
    virtual void receive(const Subscription& subscribtion, UIEvent& event) override;
    
    bool event(UIEvent& event);
    bool system_event(UIEvent& event);
    void add_zone(Zone* zone) {zones.push_back(zone);}
    Zone* find_zone(const Variant::Point& pt) {
        for(auto zone:zones) {
            if(within(pt, zone->get_position())) {
                return zone;
            }
        }
        return nullptr;
    }
    std::vector<Zone*>& get_zones() {return zones;}
    void render();
    //void update();
    void invalidate();
    LGFX_Device* get_display() {return display;}
    int get_max_zones() const {return max_zones;}
    int get_max_children() const {return max_children;}
    size_t get_max_queue_len() const {return max_queue_len;}
 
private:
    static void touch_isr();

private:
    static UIComposer* this_instance;
    LGFX_Device* display;
    ESPCirQueue<UIEvent> system_queue;
    ESPCirQueue<UIEvent> user_queue;
    UIComposer::Subscriber subscriber;
    embo::binary_semaphore semaphore;
    // UI Components
    std::vector<Zone*> zones;
    // Configuration
    // ToDo: Get from config file?
    int pin_int;    // Obtained from GFX
    int max_zones = 16;
    int max_children = 16;
    size_t max_queue_len = 0;
};

//= End of composer.h ==========================================================
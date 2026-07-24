//==============================================================================
// ui_omposer.h
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
class UIEvent;
class ImageRegistry;

using Zones = std::vector<Zone*>;
using EventQueue = ESPCirQueue<UIEvent>;
using StateAction = void (UIComposer::*)(Component* comp, Variant::Point pt);
struct Transition {
    Component::State next_state;
    StateAction action;
};

//-----------------------------------------------------------------------------
class Image 
{
public:
    Image():image_source(nullptr), image_sprite(nullptr) {}
    Image(const char* image_source);
    virtual ~Image();
    const char* get_source() {return image_source;}
    LGFX_Sprite* get_sprite() {return image_sprite;}
    void set_sprite(LGFX_Sprite* sprite) {image_sprite = sprite;}
private:
    const char* image_source;    
    LGFX_Sprite* image_sprite;
    bool    source_allocated;
};

//------------------------------------------------------------------------------
class ImageRegistry
{
public:
    ImageRegistry(UIComposer* composer) : composer(composer){}
    virtual ~ImageRegistry() {image_container.empty();}
    void reserve(int size) {image_container.resize(size);}
    void add(Image image, int image_id);
    void add(const char * image_source, int image_id);
    Image* get(int id);
    void init();

private:
    bool load_from_fs(Image* image);

private:
    UIComposer* composer;
    std::vector<Image> image_container;
};

//------------------------------------------------------------------------------
class UIComposer : public ESPEventBroker<UIEvent>
{
public:
    enum SubFlags : unsigned int{
        SUB_NONE = 0,
        SUB_SELF = 1,           // Subscribe only to events from itself
        SUB_ALL = 0xFFFFFFFF
    };
    UIComposer(LGFX_Device* display, int queue_size = 512):
    ESPEventBroker<UIEvent>(queue_size, UIEvent::EVT_LAST - Event::FIRST),
    subscriber(this),
    display(display),
    system_queue(64),
    user_queue(64),
    image_registry(this) {
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
    void add_timer(Subscription& subscription, unsigned interval);
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
    void init();
    void invalidate();
    void lock() {mutex.lock();}
    void unlock() {mutex.unlock();}
    LGFX_Device* get_display() {return display;}
    void set_focused(Component* focused) {this->focused = focused;}
    int get_max_zones() const {return max_zones;}
    int get_max_children() const {return max_children;}
    size_t get_max_queue_len() const {return max_queue_len;}

public:
    ImageRegistry image_registry;

private:
    static void touch_isr();
    // Action handlers
    void do_press(Component* comp, Variant::Point pt);
    void do_release(Component* comp, Variant::Point pt);
    void do_enter(Component* comp, Variant::Point pt);
    void do_leave(Component* comp, Variant::Point pt);
 
private:
    static UIComposer* this_instance;
    LGFX_Device* display;
    EventQueue system_queue;
    EventQueue user_queue;
    UIComposer::Subscriber subscriber;
    embo::binary_semaphore semaphore;
    embo::recursive_mutex mutex;
    // UI Components
    Zones zones;
    Components components; 
    Component* focused = nullptr;
    static const Transition StateTable[4][8];
    // Configuration
    // ToDo: Get from config file?
    int pin_int;    // Obtained from GFX
    int max_zones = 16;
    int max_children = 16;
    // Stats
    size_t max_queue_len = 0;
};

//= End of composer.h ==========================================================

//==============================================================================
// UIComponents.h
#pragma once
#include <vector>
#include <functional>
#define LGFX_USE_V1
#include <LittleFS.h>
#include <LovyanGFX.hpp>
#include <variant.h>
#include <esp_event_broker.h>
#include <ui_event.h>
#include <ui_composer.h>

class UIEvent;
class UIEventBroker;
class UIComposer;
class Zone;


using TFT_Color = int; 
using Handle = unsigned;
using Broker = ESPEventBroker<UIEvent>;
using Subscription = ESPEventBroker<UIEvent>::Subscription;
using Callback = Broker::EventCallback;

//==============================================================================
class Component : public Broker::Subscriber
{
public:
    using Children = std::vector<Component*>;
    //class Subscriber;
    Component(Component* parent, Variant::Position position);
    ~Component() {}
    void add(Component* child);
    UIComposer* get_composer() const {return reinterpret_cast<UIComposer*>(broker);}
    const Variant::Position& get_position() const  {return position;}
    bool is_visible() {return visible;}
    virtual void set_visible(bool visible = true);
    virtual void invalidate();
    virtual void render();
    virtual void update();
    Children& get_children() {return children;}
    Component* is_within(const Variant::Point& pt, Variant::Point ref = {0, 0});
    void notify_hit(const Subscription& subscription, UIEvent& event, Variant::Point ref = {0, 0});
protected:
    Variant::Position position;
    Component* parent = nullptr;
    Zone* zone = nullptr;
    Children children;
    bool visible = false;
};

//==============================================================================
class Zone : public Component
{
public:
    Zone(UIComposer* composer, Variant::Position position) 
    : Component(nullptr, position) {
        this->broker = reinterpret_cast<ESPEventBroker<UIEvent>*>(composer);
        this->zone = this;
        this->sprite = nullptr;
        this->visible = true; //Zones are visible
    }
    ~Zone() {
        delete(sprite);
    }
    LGFX_Sprite* get_sprite() { return sprite; };
    void set_frame(TFT_Color color, bool frame = true) {this->frame = frame;}
    void set_bg_color(TFT_Color color) { this->bg_color = color;}
    virtual void render() override;
    virtual void update() override;

protected:
    LGFX_Sprite* sprite;
    bool frame = false;
    TFT_Color frame_color;
    TFT_Color bg_color = 0;
};

//==============================================================================
class Panel : public Component
{
public:
    Panel(Component* parent, Variant::Position position) : Component(parent, position) {}
    void set_bg_color(TFT_Color color) {this->bg_color = color;}
    int get_bg_color() const {return bg_color; }
    virtual void render() override;
    virtual void update() override;
    virtual void set_visible(bool visible = true) override;
private:
    int bg_color;
};

//==============================================================================
class Canvas : public Component 
{
public:

};

//==============================================================================
class Text : public Component 
{
public:
    Text(Component* parent, Variant::Position position) : Component(parent, position) {}
    const char* get_text() const {return text;}
    const int get_bg_color() const {return bg_color;}
    const int get_text_color() const {return text_color;}
    void set_text(const char* text) {strncpy(this->text, text, sizeof(this->text) -1);}
    void set_bg_color(int color) {this->bg_color = color;}
    void set_text_color(int color) {this->text_color = color;}
    void set_font(const GFXfont& font) {this->font = font;}
    virtual void render() override;
    virtual void update() override;
private:
    char text[128];
    int text_color = TFT_BLACK;
    int bg_color = TFT_WHITE;
    GFXfont font = fonts::DejaVu12;
};


//==============================================================================
class Icon : public Component {
    enum Transparency {
        NONE,
        COLOR,
        POINT
    };
public:
    Icon(Component* parent, Variant::Position position, const char* file_path) :
        Component(parent, position),
        sprite(nullptr),
        transparency(NONE),
        transparent_color(TFT_BLACK) {
            strncpy(this->file_path, file_path, sizeof(this->file_path) - 1);
        }
    void render() override;
    void update() override;

    void set_transparent(TFT_Color color);
    void set_transparent(Variant::Point point);

private:
    Handle handle;
    LGFX_Sprite* sprite;
    char file_path[64];

private:
    bool load_from_fs();
    TFT_Color transparent_color;
    Variant::Point transparent_point;
    Transparency transparency = NONE;
};

//==============================================================================
class Button : public Component
{
public:
    Button(Component* parent, Variant::Position position) : Component(parent, position) {}
    void set_bg_color(TFT_Color color) {this->bg_color = color;}
    void set_text_color(TFT_Color color) {this->text_color = color;}
    int get_bg_color() const {return bg_color; }
    int get_text_color() const {return text_color; }
    void set_label(const char* label) {strncpy(this->label, label, sizeof(this->label));}
    const char* get_label() const {return label;}
    virtual void render() override;
    virtual void update() override;
private:
    LGFX_Button* button = nullptr;
    int bg_color = 0;
    int text_color = 0;
    char label[32] = "";
};

//==============================================================================

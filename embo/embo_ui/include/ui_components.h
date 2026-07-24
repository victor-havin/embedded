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
//==============================================================================
//#define DEBUG_PRINT

void debug_print_component(Component* component);

static const Variant::Position null_pos = {{0,0},{0,0}};

class UIEvent;
class UIEventBroker;
class UIComposer;
class ImageRegistry;
class Zone;
class Component;
class Container;


using TFT_Color = int; 
using Handle = unsigned;
using Broker = ESPEventBroker<UIEvent>;
using Subscription = ESPEventBroker<UIEvent>::Subscription;
using Callback = Broker::EventCallback;
using Components = std::vector<Component*>;


//==============================================================================
class Layout
{
    friend class Container;

public:
    enum {
        HORIZONTAL      =  1 << 0,
        VERTICAL        =  1 << 1,
        ASSERTIVE       =  1 << 2,
    };

public:
    Layout(Container* container, unsigned properties);
    virtual void render() = 0;

protected:
    Container* container;
    unsigned properties;
};


//==============================================================================
class DockedLayout : public Layout
{
public:
    DockedLayout(Container* container, unsigned properties = 0) :
        Layout(container, properties) {}
    virtual void render();
};

//==============================================================================
class StackedLayout : public Layout
{
public:
    StackedLayout(Container* container, unsigned properties) :
        Layout(container, properties) {}
    virtual void render();

private:    
    void stack_horizontally(Components& children, Variant::Position parent_position);
    void stack_vertically(Components& children, Variant::Position parent_position);

};

//==============================================================================
class TableLayout : public Layout
{
public:
    TableLayout(Container* container, unsigned properties, 
        int columns,  int rows = 0, int row_height = 0) :
        Layout(container, properties),
        n_rows(rows), n_columns(columns), row_height(row_height) {}
    virtual void render();
    bool set_column(int column, int width);

private:
    void process_table(Components& children, Variant::Position parent_position);

private:
    int n_columns;
    int n_rows;
    int row_height;
    int* columns = nullptr;
};

//==============================================================================
// class Component
// Component is the base class for all UI components.
// Use it to create leaf nodes of screen layout. 
//
class Component : public Broker::Subscriber
{
    friend class Layout;

public:
    friend class Frame;
    // State cells for individual components
    enum : byte {
        IS_VISIBLE  = 1 << 0,
        IS_DIRTY    = 1 << 1,
    };
    // Transition token components
    enum : byte {
        IS_INSIDE   = 1 << 0,
        IS_DOWN     = 1 << 1,
        WAS_DOWN    = 1 << 2,
    };
    // Component kind
    enum class Kind : unsigned {
        UNKNOWN,
        ZONE,
        PANEL,
        WIDGET,
        WINPUT
    };
    // Component state
    enum class State : unsigned {
        IDLE    = 0,
        DRAGGED = 1,
        PRESSED = 2,
        SLIDOUT = 3
    };

    struct Props {
        unsigned    container:1;
        unsigned    kind:3;
        unsigned    state:2;
    };

public:
    Component(Container* parent, Variant::Position position = null_pos);
    UIComposer* get_composer() const {return reinterpret_cast<UIComposer*>(broker);}
    const Variant::Position& get_position() const  {return position;}
    virtual void gather(Components& components);
    virtual void set_visible(bool visible = true);
    virtual void invalidate();
    virtual void render();
    virtual void init(){};
    virtual void update(){};
    virtual void set_position(Variant::Position position) {this->position = position;}

    bool is_container() const {return props.container;};
    bool is_visible();
    bool is_dirty();
    void set_dirty(bool dirty = true);
    bool is_input(){return get_kind() == Kind::WINPUT;}
    Container* get_parent() const {return parent;}
    bool is_within_abs(const Variant::Point& pt);
    Variant::Position get_position() {return position;}
    Variant::Position get_absolute() const;
    Variant::Position get_zone_rel() const;
    State get_state() {return static_cast<State>(props.state);}
    void set_state(const State state) { props.state = static_cast<unsigned>(state); }
    Kind get_kind() { return static_cast<Kind>(props.kind); }

protected:
    void set_kind(Kind kind) { props.kind = static_cast<unsigned>(kind); };

protected:
    Variant::Position position;
    Container* parent = nullptr;
    Zone* zone = nullptr;
    Props props;
    std::atomic<byte> status;
};

//==============================================================================
// Class Container
// Container is component with children.
// Used for creating objects hosting other objects. For example,
// Zone and Panel
//
class Container : public Component
{
public:
    friend class Layout;
public:
    using Children = Components;
    //class Subscriber;
    Container(Container* parent, Variant::Position position = null_pos);
     ~Container() {
        for(auto c:children) delete c;
    }

    TFT_Color get_bg_color() const {return bg_color;}
    void set_bg_color(TFT_Color color) { 
        this->bg_color = color;
        set_dirty();
    }

    virtual void gather(Components& components);
    virtual void set_visible(bool visible = true);
    virtual void invalidate();
    virtual void render();
    virtual void init();
    virtual void update();

    Children& get_children() {return children;}
    void add(Component* child);
 
protected:
    void set_layout(Layout* layout);

protected:
    Children children;
    TFT_Color bg_color = TFT_WHITE;
    Layout* layout = nullptr;
};

//==============================================================================
class Zone : public Container
{
public:
    Zone(UIComposer* composer, Variant::Position position) ;
    ~Zone() {
        if(sprite) delete sprite;
        if(layout) delete layout;
    }
    LGFX_Sprite* get_sprite() { return sprite; };
    TFT_Color get_fg_color() const {return fg_color;}
    void set_fg_color(TFT_Color color) {fg_color = color;}
    TFT_Color get_text_color() const {return text_color;}
    void set_text_color(TFT_Color color) {text_color = color;}
    const lgfx::IFont* get_font() const {return font;}
    void set_font(const lgfx::IFont* font) {this->font = font;}
    void switch_visible(Component* component);

    virtual void render() override;
    virtual void init() override;
    virtual void update() override;

protected:
    Layout* layout = nullptr;
    LGFX_Sprite* sprite;
    TFT_Color fg_color = TFT_BLACK;
    TFT_Color text_color = TFT_BLACK;
    const lgfx::IFont* font = nullptr;
    Component* current_visible = nullptr;
};

//==============================================================================
class Frame
{
public:
    Frame(Component* component) : component(component) {
        set_color(component->zone->get_fg_color());
    }
    void set_color(TFT_Color color) {this->color = color;}
private:
    Component* component;
    TFT_Color color = TFT_BLACK;
};

//==============================================================================
class Panel : public Container
{
public:
    Panel(Container* parent, Variant::Position position = null_pos) : 
        Container(parent, position) {
            set_kind(Kind::PANEL);
            set_bg_color(zone->get_bg_color());
            set_text_color(zone->get_text_color());
            set_dirty();
    }
    void set_text_color(TFT_Color color) {
        this->text_color = color;
        set_dirty();
    }
    TFT_Color get_text_color() const {return text_color; }
    virtual void update() override;

private:
    TFT_Color text_color;
    Frame* frame = nullptr;
};

//==============================================================================
class Canvas : public Component
{
public:
    Canvas(Container* parent, Variant::Position position = null_pos) : 
        Component(parent, position) {
            set_kind(Kind::WIDGET);
            Variant::Position rel = get_zone_rel();
        }

    virtual void update() override;

    LGFX_Sprite* get_sprite();

private:
        void init_sprite();
private:
        LGFX_Sprite* sprite = nullptr;
};

//==============================================================================
class Text : public Component 
{
public:
    Text(Container* parent, Variant::Position position = null_pos, int16_t len = 16);
    virtual ~Text() {
        delete[] text;
    }
    const char* get_text() const {return text;}
    const int get_bg_color() const {return bg_color;}
    const int get_text_color() const {return text_color;}
    void set_text(const char* text);
    const char* operator = (const char* text);
    void set_bg_color(TFT_Color color);
    void set_text_color(TFT_Color color);
    void set_text_color(TFT_Color bg_color, TFT_Color text_color);
    void set_font(const lgfx::IFont* font);
    virtual void render() override;
    virtual void update() override;

protected:
    char* text;
    int16_t len;
    int text_color = TFT_BLACK;
    int bg_color = TFT_WHITE;
    const lgfx::IFont* font;
};

//==============================================================================
//
class Input : public Component {
public:
    Input(Container* parent, Variant::Position = null_pos);
    virtual ~Input() {}
    virtual void render() override;
    void focus();

protected:
    virtual void set_focused(bool focused = true);

protected:
    bool focused = false;
};

//==============================================================================
//
class TextInput : public Input {
public:
    TextInput(Container* parent, Variant::Position = null_pos, int len = 16);
    virtual ~TextInput();

    virtual void set_visible(bool visible = true) override;
    virtual void update() override;
    virtual void set_position(Variant::Position position) override;

    void set_focused_color(TFT_Color bg_color, TFT_Color text_color) {
        focused_bg_color = bg_color;
        focused_text_color = text_color;
    }
    
    void set_text(const char* text);
    void set_bg_color(TFT_Color color);
    void set_text_color(TFT_Color color);
    void set_text_color(TFT_Color bg_color, TFT_Color text_color);
    void set_font(const lgfx::IFont* font);

protected:
    virtual void set_focused(bool focused = true) override;

protected:
    Text*       text;
    TFT_Color   focused_bg_color;
    TFT_Color   focused_text_color;
};

//==============================================================================
class NumericInput : public TextInput {
public:
    void set_text(const char* text) = delete;
    void set_number(int number);
    int  get_number();
    
protected:
    int number = 0;
};

//==============================================================================
class Icon : public Component {
    friend class IconAlias;
protected:
    struct Frame {
        int id;
        LGFX_Sprite* sprite;
        TFT_Color transparent;
    };
    enum Transparency {
        NONE,
        COLOR,
        POINT
    };
public:
    Icon(Container* parent, Variant::Position position = null_pos, int id = -1);
    virtual ~Icon() {}

    virtual void init() override;
    virtual void render() override;
    virtual void update() override;
    virtual bool is_multi_icon() {return false;}
    bool load(int id = -1);
    void set_transparent(TFT_Color color);
    void set_transparent(Variant::Point point);

protected:
    Variant::Point transparent_point;
    Transparency transparency = NONE;
    TFT_Color transparent_color = TFT_BLACK;
    Frame frame;
};

//==============================================================================
class MultiIcon : public Icon
{
public:
    MultiIcon(Container* parent, Variant::Position position = null_pos);
    virtual ~MultiIcon();

    void add(int id);
    void select(int index);
    virtual bool is_multi_icon() {return true;}

protected:
    std::vector<Icon::Frame> frames;
};

//==============================================================================
class Button : public Component
{
public:
    Button(Container* parent, Variant::Position position = null_pos, size_t len = 16) : 
    Component(parent, position),
    len(len) {
        set_kind(Kind::WIDGET);
        label = new char[len+1];
        bg_color = zone->get_bg_color();
        text_color = zone->get_text_color();
    }
    virtual ~Button() {
        if(button) delete button;
        delete[] label;
    }
    void set_bg_color(TFT_Color color) {this->bg_color = color;}
    void set_text_color(TFT_Color color) {this->text_color = color;}
    int get_bg_color() const {return bg_color; }
    int get_text_color() const {return text_color; }
    const lgfx::IFont* get_font() const {return font;}
    void set_font(const lgfx::IFont* font) {this->font = font;}
    void set_label(const char* label) {
        strncpy(this->label, label, len);
        set_dirty();
    }
    const char* get_label() const {return label;}
    virtual void render() override;
    virtual void init() override;
    virtual void update() override;
private:
    LGFX_Button* button = nullptr;
    int bg_color;
    int text_color;
    size_t len;
    char* label;
    const lgfx::IFont* font;
};

//= End of components.h ========================================================

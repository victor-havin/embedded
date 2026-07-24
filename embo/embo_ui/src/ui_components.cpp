//==============================================================================
#include <ui_composer.h>
#include <ui_components.h>
#include <ui_event.h>

//==============================================================================
// Helper functions

void debug_print_component(Component* component)
{
    const char* kinds[] = {
        "UNKNOWN",
        "ZONE",
        "PANEL",
        "WIDGET"
    };
    Variant::Position pos = component->get_position();
    Component::Kind kind = component->get_kind();
    if(kind < Component::Kind::UNKNOWN || kind > Component::Kind::WIDGET) 
        kind = Component::Kind::UNKNOWN;
    Serial.printf("Component %s @ {{%d,%d},{%d,%d}}\n",
        kinds[(int)kind], pos.p.x, pos.p.y, pos.s.w, pos.s.h
    );
}

//==============================================================================
// Class Component implementation

//---------------------------------------------------------------------------------
// Constructor
Component::Component(Container* parent, Variant::Position position) : 
Subscriber(reinterpret_cast<ESPEventBroker<UIEvent>*>(parent ? parent->get_composer() : nullptr)),
parent(parent),  position(position) {
    status.store(0, std::memory_order_relaxed);
    set_active(false); // Not active untill visible
    set_dirty(true);
    props.container = false;
    props.kind = static_cast<unsigned>(Component::Kind::UNKNOWN);
    props.state = static_cast<unsigned>(Component::State::IDLE);
    if(parent) {
        this->zone = parent->zone;
        parent->add(this);
    }
}

//--------------------------------------------------------------------------------
void Component::gather(Components& components) {
    Variant::Position pos = this->get_position();
#ifdef DEBUG_PRINT
    Serial.printf("Gathered component  at {{%d,%d},{%d, %d}}\n", 
        pos.p.x, pos.p.y, pos.s.h, pos.s.w
    );
#endif
    components.push_back(static_cast<Component*>(this));
}

//--------------------------------------------------------------------------------
bool Component::is_visible() {
    return !!(status.load(std::memory_order_acquire) & IS_VISIBLE);
}

//--------------------------------------------------------------------------------
bool Component::is_dirty() {
    return !!(status.load(std::memory_order_acquire) & IS_DIRTY);
}

//--------------------------------------------------------------------------------
void Component::set_dirty(bool dirty) {
    if (dirty) {
        status.fetch_or(IS_DIRTY, std::memory_order_release);
    } else {
        status.fetch_and(static_cast<byte>(~IS_DIRTY), std::memory_order_release);
    }
}

//--------------------------------------------------------------------------------
// invalidate
void Component::invalidate() {
    TaskHandle_t this_handle = xTaskGetCurrentTaskHandle();
    pthread_t this_id;
    memcpy(&this_id, &this_handle, sizeof(pthread_t));
    pthread_t consumer_id = get_composer()->get_consumer_id();
    if(this_id == consumer_id) {
        // Called from the broker consumer thread. Simply update the component
        update();
    } else {
        // Called from the user thread. Send the update event.
        UIEvent event(UIEvent::EVT_UPDATE, zone);
        get_composer()->event(event);
    }
}

//--------------------------------------------------------------------------------
// set_visible
void Component::set_visible(bool visible) {
    get_composer()->lock();
    bool was_visible = is_visible();
    bool changed = false;
    if(was_visible && !visible) {
        changed = true;
        // Change visibility to invisible
        this->set_active(false);
    }
    else if(!was_visible && visible) {
        changed = true;
        // Change visibility to visible
        this->set_active();
        this->set_dirty();
    }
    if(changed) {
        if (visible) {
            status.fetch_or(IS_VISIBLE, std::memory_order_release);
        } else {
            status.fetch_and(static_cast<byte>(~IS_VISIBLE), std::memory_order_release);
        }
    }
    get_composer()->unlock();
}


//--------------------------------------------------------------------------------
bool Component::is_within_abs(const Variant::Point& pt) {
    if (!is_visible()) return false;
    Variant::Position abs_position = get_absolute();
    return within(pt, abs_position);
}

//--------------------------------------------------------------------------------
Variant::Position Component::get_absolute() const {
    Variant::Position abs_position = position; // Start with local relative coordinates
    Component* parent_ptr = parent;

    // Walk up the hierarchy to accumulate parent relative offsets
    while (parent_ptr != nullptr) {
        abs_position.p.x += parent_ptr->position.p.x;
        abs_position.p.y += parent_ptr->position.p.y;
        parent_ptr = parent_ptr->parent;
    }
    return abs_position;
}

//--------------------------------------------------------------------------------
Variant::Position Component::get_zone_rel() const {
    Variant::Position rel_position = position;
    Component* parent_ptr = parent;
    if(parent_ptr) {
        while(parent_ptr->get_parent()) {
            Variant::Position parent_pos = parent_ptr->get_position();
            rel_position.p.x += parent_pos.p.x;
            rel_position.p.y += parent_pos.p.y;
            parent_ptr = parent_ptr->get_parent();
        }
    }
    return rel_position;
}

//--------------------------------------------------------------------------------
void Component::render() {
}

//==============================================================================
Container::Container(Container* parent, Variant::Position position) :
Component(parent, position) {
    UIComposer* composer = get_composer();
    if(composer) {
        children.reserve(composer->get_max_children());
    }
    if(parent) {
        set_bg_color(parent->get_bg_color());
    }
    props.container = true;
}

//--------------------------------------------------------------------------------
void Container::set_visible(bool visible) {
    Component::set_visible(visible);
    for(auto child:children) {
        child->set_visible(visible);
    }
}

//--------------------------------------------------------------------------------
void Container::add(Component* child) {
    if(child) {
        children.push_back(child);
    }
}

//--------------------------------------------------------------------------------
void Container::gather(Components& components) {
    Variant::Position pos = this->get_position();
#ifdef DEBUG_PRINT
    Serial.printf("Gathered component  at {{%d,%d},{%d, %d}}\n", 
        pos.p.x, pos.p.y, pos.s.h, pos.s.w
    );
#endif
    components.push_back(this);
    for(auto child:get_children()) {
        child->gather(components);
    }
}

//--------------------------------------------------------------------------------
// render
void Container::render() {
    if(layout) {
        layout->render();
    }
    for(auto child:children) {
        child->render();
    }
}

//--------------------------------------------------------------------------------
void Container::init() {
    for(auto child:children) {
        child->init();
    }
}

//--------------------------------------------------------------------------------
//
void Container::update() {
    for(auto child:children) {
        child->update();
    }
}

//------------------------------------------------------------------------------
void Container::invalidate() {
    Component::invalidate();
}

//------------------------------------------------------------------------------
void Container::set_layout(Layout* layout){
    if(this->layout) delete this->layout;
    this->layout = layout;
}   
//==============================================================================
// Class Layout implementation

Layout::Layout(Container* container, unsigned properties):
    container(container),
    properties(properties) {
    container->set_layout(this);
}

//==============================================================================
// Class DockedLayout implementation

void DockedLayout::render() {
    Variant::Position container_position = container->get_position();
    asm volatile("" : "+m" (container_position)); 
    for(auto component:container->get_children()) {
        Variant::Position pos = null_pos;
        pos.s = container->get_position().s;
        component->set_position(pos);
     }
}

//==============================================================================
// Class StackedLayout implementation
void StackedLayout::render() {
    Components& children = container->get_children();
    Variant::Position position = container->get_position();
    if(properties & Layout::HORIZONTAL) stack_horizontally(children, position);
    else if(properties & Layout::VERTICAL) stack_vertically(children, position);
}

//------------------------------------------------------------------------------
void StackedLayout::stack_horizontally(Components& children, Variant::Position parent_position)
{
    Variant::Position next_pos = null_pos;
    int increment = parent_position.s.w / children.size();
    for(auto child:children) {
        Variant::Position child_pos = child->get_position();
        if(properties & Layout::ASSERTIVE) {
            next_pos.s.w = increment;
            next_pos.s.h = container->get_position().s.h;
        } else {
            next_pos.s = child_pos.s;
        }
        child_pos = next_pos;
        child->set_position(child_pos);
        next_pos.p.x += (properties & Layout::ASSERTIVE) ?
            increment : child_pos.s.w;
    }
}

//------------------------------------------------------------------------------
void StackedLayout::stack_vertically(Components& children, Variant::Position parent_position)
{
    Variant::Position next_pos = null_pos;
    int increment = parent_position.s.h / children.size();

    for(auto child:children) {
        Variant::Position child_pos = child->get_position();
        if(properties & Layout::ASSERTIVE) {
            next_pos.s.h = increment;
            next_pos.s.w = container->get_position().s.w;
        } else {
            next_pos.s = child_pos.s;
        }
        child_pos = next_pos;
        child->set_position(child_pos);
        next_pos.p.y += (properties & Layout::ASSERTIVE) ?
            increment : child_pos.s.h;
    }
}

//==============================================================================
// Class TableLayout implementation

//------------------------------------------------------------------------------
bool TableLayout::set_column(int column, int width) {
    if(column > n_columns) return false;
    if(!columns) {
        columns = new int[n_columns];
        for(int c = 0; c < n_columns; c++) columns[c] = 0;
    }
    columns[column] = width;
    return true;
}

//------------------------------------------------------------------------------
// render
void TableLayout::render() {
    Components& children = container->get_children();
    Variant::Position position = container->get_position();
    int undefined = 0;
    int total_width = 0;
    int default_width = 0;
    if(columns) {
        // Process user-defined column information
        // and complete it if necessery
        for(auto column = 0; column < n_columns; column++) {
            if(columns[column]) total_width += columns[column];
            else undefined++;
        }
        if(total_width < container->get_position().s.w) {
            // Divide evenly between undefined
            default_width = container->get_position().s.w / undefined;
        }
        for(auto column = 0; column < n_columns; column++) {
            if(!columns[column]) columns[column] = default_width;
        }
    }
    process_table(children, position);
}

//------------------------------------------------------------------------------
// process_table
void TableLayout::process_table(Components& children, Variant::Position parent_position) {
    int n_children = children.size();
    n_rows = n_rows ? n_rows : n_children / n_columns;
    Variant::Position next_pos = null_pos;
    Variant::Size step;
    bool assertive = properties & Layout::ASSERTIVE;
    if(assertive) {
        // Compute assertive steps
        step.w = parent_position.s.w / n_columns;
        step.h = parent_position.s.h / n_rows;
    }
    // If row height is explicitly defined - use it
    if(row_height) step.h = row_height;
    next_pos.s = step;
    // Process rows and columns
    for(auto row = 0; row < n_rows; row++) {
        int n_child = row * n_columns;
        if(n_child > n_children) break;
        if(!assertive) {
            for(auto column = 0; column < n_columns; column++) {
                // Select max height for a row
                n_child = column + row * n_columns;
                if(n_child > n_children) break;
                Variant::Position next_pos = children[n_child]->get_position();
                if(step.h < next_pos.s.h) step.h = next_pos.s.h;
            }
        }
        for(auto column = 0; column < n_columns; column++) {
            int n_child = column + row * n_columns;
            if(n_child > n_children) break;
            Component* comp = children.at(n_child);
            Variant::Position child_pos = comp->get_position();
            if(columns) next_pos.s.w = columns[column];
            child_pos = next_pos;
            comp->set_position(child_pos);
            if(columns) next_pos.p.x += columns[column];
            else next_pos.p.x += step.w;
        }
        next_pos.p.x = 0;
        next_pos.p.y += step.h;
    }
}

//==============================================================================

//--------------------------------------------------------------------------------
Zone::Zone(UIComposer* composer, Variant::Position position) 
: Container(nullptr, position) {
    this->broker = reinterpret_cast<ESPEventBroker<UIEvent>*>(composer);
    // Reserve children because component can't do it with null parent.
    children.reserve(get_composer()->get_max_children());
    this->zone = this;
    this->sprite = nullptr;
    this->current_visible = nullptr;
    this->font = &lgfx::fonts::Font2;
    set_kind(Kind::ZONE);
    // Zones are always visible and active
    set_visible(); 
    set_active();
    if(composer) {
        composer->add_zone(this);
    }
}

//--------------------------------------------------------------------------------
void Zone::render() {
    Container::render();
}

//--------------------------------------------------------------------------------
void Zone::init() {
    // Create sprite 
    if(sprite == nullptr) {
        sprite = new LGFX_Sprite(this->get_composer()->get_display());
        sprite->setPsram(true);
        sprite->setColorDepth(16);
        sprite->setFont(font);
        sprite->createSprite(position.s.w, position.s.h);
    }
    Container::init();
}

//--------------------------------------------------------------------------------
void Zone::update() {
    if (!is_visible()) return;
    // If current visible not set, set to the visble child.
    if(current_visible == nullptr) {
        for(auto child:children) {
            if(child->is_visible()) {
                current_visible = child;
                break;
            }
        }
    }
    Container::update();
    sprite->pushSprite(get_composer()->get_display(), this->position.p.x, this->position.p.y);
    return;
}

//------------------------------------------------------------------------------
void Zone::switch_visible(Component *component) {
    Component* old_visible = current_visible;
    for(auto child:children) {
        if(child == component) {
            current_visible = child;
            if(old_visible) {
                old_visible->set_visible(false);
            }
            child->set_visible(true);
            child->set_dirty();
        }
    }
    invalidate();
}

//==============================================================================

//------------------------------------------------------------------------------
void Panel::update() {
    if (!is_visible()) return;
    if(is_dirty()) {
        LGFX_Sprite* sprite = this->zone->get_sprite();
        sprite->fillRect(position.p.x, position.p.y, position.s.w, position.s.h, bg_color);
        set_dirty(false);
    }
    Container::update();
}

//==============================================================================

//------------------------------------------------------------------------------
Text::Text(Container* parent, Variant::Position position, int16_t len) : 
    Component(parent, position),
    len(len) {
        set_kind(Kind::WIDGET);
        text = new char[len + 1];
        text[0] = 0;
        bg_color = parent->get_bg_color();
        text_color = zone->get_text_color();
        font = zone->get_font();
}

//------------------------------------------------------------------------------
void Text::set_text(const char* text) {
    strncpy(this->text, text, len);
    set_dirty();
}

//------------------------------------------------------------------------------
const char* Text::operator = (const char* text)  {
    set_text(text);
    return this->text;
}

//------------------------------------------------------------------------------
void Text::set_bg_color(TFT_Color color) {
    this->bg_color = color;
    set_dirty();
}

//------------------------------------------------------------------------------
void Text::set_text_color(TFT_Color color) {
    this->text_color = color;
    set_dirty();
}

//------------------------------------------------------------------------------
void Text::set_text_color(TFT_Color bg_color, TFT_Color text_color) {
    this->bg_color = bg_color;
    this->text_color = text_color;
    set_dirty();
}

//------------------------------------------------------------------------------
void Text::set_font(const lgfx::IFont* font) {
    this->font = font;
    set_dirty();
}


//------------------------------------------------------------------------------
void Text::render() {

}

//------------------------------------------------------------------------------
void Text::update() {
    if(!is_visible()) return;
    if(!is_dirty()) return;
    LGFX_Sprite* sprite = this->zone->get_sprite();
    Variant::Position rel = get_zone_rel();
    const lgfx::IFont* saved_font = sprite->getFont();
    sprite->setClipRect(rel.p.x, rel.p.y, rel.s.w, rel.s.h);
    sprite->setCursor(rel.p.x, rel.p.y);
    sprite->fillRect(rel.p.x, rel.p.y, rel.s.w, rel.s.h, bg_color);
    sprite->setTextColor(text_color, bg_color);
    sprite->setFont(font);
    sprite->print(text);
    sprite->clearClipRect();
    sprite->setFont(saved_font);
    set_dirty(false);
}
//==============================================================================

//------------------------------------------------------------------------------
Input::Input(Container* parent, Variant::Position position) :
Component(parent, position) {
    set_kind(Kind::WINPUT);
};

//------------------------------------------------------------------------------
void Input::render() {
    subscribe(Subscription(UIEvent::EVT_FOCUS, this, 0, 
        [this](const Subscription& s, const UIEvent& e){
            if(this != e.get_source()) {
                set_focused(false);
            }
        }
    ));
}

//------------------------------------------------------------------------------
void Input::focus() {
    set_focused();
}

//------------------------------------------------------------------------------
void Input::set_focused(bool focused) {

}

//==============================================================================
//
//------------------------------------------------------------------------------
TextInput::TextInput(Container* parent, Variant::Position position, int len) :
Input(parent, position) {
    text = new Text(parent, position, len);
    focused_bg_color = ~zone->get_bg_color();
    focused_text_color = ~zone->get_text_color();
}

//------------------------------------------------------------------------------
TextInput::~TextInput() {
    delete text;
}

//------------------------------------------------------------------------------
void TextInput::set_position(Variant::Position position) {
    Component::set_position(position);
    text->set_position(position);
}

//------------------------------------------------------------------------------
void TextInput::set_visible(bool visible) {
    Component::set_visible(visible);
    text->set_visible(visible);
}

//------------------------------------------------------------------------------
void TextInput::update() {
    if(!is_visible()) return;
    if(!is_dirty()) return;
    text->update();
    Input::update();
}

//------------------------------------------------------------------------------
void TextInput::set_text(const char* text) {
    this->text->set_text(text);
    this->text->set_dirty();
    set_dirty();
}

//------------------------------------------------------------------------------
void TextInput::set_bg_color(TFT_Color color) {
    text->set_bg_color(color);
    text->set_dirty();
    set_dirty();
}

//------------------------------------------------------------------------------
void TextInput::set_text_color(TFT_Color color) {
    text->set_text_color(color);
    text->set_dirty();
    set_dirty();
}

//------------------------------------------------------------------------------
void TextInput::set_text_color(TFT_Color bg_color, TFT_Color text_color) {
    text->set_bg_color(bg_color);
    text->set_text_color(text_color);
    text->set_dirty();
    set_dirty();
}

//------------------------------------------------------------------------------
void TextInput::set_font(const lgfx::IFont* font) {
    text->set_font(font);
    text->set_dirty();
    set_dirty();
}

//------------------------------------------------------------------------------
void TextInput::set_focused(bool focused) {
    if(this->focused != focused) {
        this->focused = focused;
        TFT_Color saved;
        saved = text->get_bg_color();
        text->set_bg_color(focused_bg_color);
        focused_bg_color = saved;
        saved = text->get_text_color();
        text->set_text_color(focused_text_color);
        focused_text_color = saved;
        text->set_dirty();
        zone->update();
    }
}

//==============================================================================

//------------------------------------------------------------------------------
void Button::render() {
    Subscriber* subscriber = (Subscriber*)this;
    subscribe(Subscription(UIEvent::EVT_ENTER, this, UIComposer::SUB_NONE, 
    [this](const Subscription& subscription, const UIEvent& event) {
        Button* button = static_cast<Button*>(event.get_source());
        UIEvent btn_event(UIEvent::EVT_PRESS, this);
        if(this == button)
            get_composer()->system_event(btn_event);
    }));
    subscribe(Subscription(UIEvent::EVT_LEAVE, this, UIComposer::SUB_NONE, 
    [this](const Subscription& subscription, const UIEvent& event) {
        Button* button = static_cast<Button*>(event.get_source());
        UIEvent btn_event(UIEvent::EVT_RELEASE, this);
        if(this == button)
            get_composer()->system_event(btn_event);
    }));
}

//--------------------------------------------------------------------------------
void Button::init() {
    LGFX_Sprite* sprite = this->zone->get_sprite();
    if(button == nullptr) {
        button = new LGFX_Button();
        button->initButtonUL(
            sprite, 
            this->position.p.x, this->position.p.y, 
            this->position.s.w, this->position.s.h, 
            TFT_WHITE, bg_color, text_color, this->label);   

    }
}

//--------------------------------------------------------------------------------
void Button::update() {
    if (!is_visible()) return;
    if(!is_dirty()) return;

    button->setLabelText(this->label);
    button->setFillColor(this->bg_color);
    button->setTextColor(this->text_color);
    button->drawButton(false, this->label);
    set_dirty(false);
}

//==============================================================================
// Icon implemntation

Icon::Icon(Container* parent, Variant::Position position, int id) :
    Component(parent, position),
    transparency(NONE) {
    set_kind(Kind::WIDGET);
    frame = {id, nullptr, 0};
};

//------------------------------------------------------------------------------
void Icon::render() {
    // Image processed lazily in update()
}

//------------------------------------------------------------------------------
void Icon::init() {
    load();
}

//------------------------------------------------------------------------------
void Icon::update() {
    if(!is_visible()) return;
    if(!is_dirty()) return;

    if(!frame.sprite) return;
    LGFX_Sprite* zone_sprite = this->zone->get_sprite();
    if(!zone_sprite) return;
    Variant::Position rel_position = get_zone_rel();
    switch(transparency) {
        case Icon::POINT:
            frame.transparent = frame.sprite->readPixel(transparent_point.x, transparent_point.y);
            break;
        case Icon::COLOR:
            frame.transparent = transparent_color;
            break;
        default:
            frame.transparent = 0;
            break;
    }
    if(transparency == Icon::NONE) {
        frame.sprite->pushSprite(zone_sprite, rel_position.p.x, rel_position.p.y);  
    } else {
        zone_sprite->fillRect(
            rel_position.p.x, rel_position.p.y, rel_position.s.w, rel_position.s.h, 
            parent->get_bg_color());
        frame.sprite->pushSprite(zone_sprite, rel_position.p.x, rel_position.p.y, frame.transparent);  
    }
    set_dirty(false);
}

//------------------------------------------------------------------------------
void Icon::set_transparent(Variant::Point point) {
    transparency = Icon::POINT;
    transparent_point = point;
}

//------------------------------------------------------------------------------
void Icon::set_transparent(TFT_Color color) {
    transparency = Icon::COLOR;
    transparent_color = color;
}

//------------------------------------------------------------------------------
bool Icon::load(int id){
    if(id == -1) id = frame.id;
    if(id == -1) return false;
    ImageRegistry& reg = get_composer()->image_registry;
    Image* img = reg.get(id);
    if(img) {
        frame.id = id;
        frame.sprite = img->get_sprite();
        set_dirty();
        return true;
    }
    return false;
}

//==============================================================================
//
//------------------------------------------------------------------------------
MultiIcon::MultiIcon(Container* parent, Variant::Position position) :
Icon (parent, position) {
}

//------------------------------------------------------------------------------
MultiIcon::~MultiIcon() {
}

//------------------------------------------------------------------------------
void MultiIcon::add(int id) {
    Frame frm;
    Image* img = get_composer()->image_registry.get(id);
    if(img) {
        frm.id = id;
        frm.sprite = img->get_sprite();
        frm.transparent = 0;
        frames.push_back(frm);
        if(frames.size() == 1)
            frame = frm;
        set_dirty();
    }
}

//------------------------------------------------------------------------------
void MultiIcon::select(int index) {
    if(index >= 0 && index < frames.size()) {
        frame = frames[index];
        load();
    }
}

//==============================================================================
//
//------------------------------------------------------------------------------
LGFX_Sprite* Canvas::get_sprite() {
    init_sprite();
    set_dirty(); // So the next invalidation redraws it.
    return sprite;
}

//------------------------------------------------------------------------------
void Canvas::update() {
    init_sprite();
    Variant::Position rel = get_zone_rel();
    LGFX_Sprite* zone_sprite = zone->get_sprite();

    zone_sprite->setClipRect(rel.p.x, rel.p.y, rel.s.w, rel.s.h);

    sprite->pushSprite(zone_sprite, rel.p.x, rel.p.y);
    zone_sprite->clearClipRect();
    set_dirty(false);

}

//------------------------------------------------------------------------------
void Canvas::init_sprite()  {
    if(sprite == nullptr) {
        sprite = new LGFX_Sprite(get_composer()->get_display());
        sprite->setPsram(true);
        sprite->setColorDepth(16);
        sprite->createSprite(position.s.w, position.s.h);
        sprite->setPaletteColor(0, TFT_GRAY); 
        sprite->setFont(&lgfx::fonts::DejaVu12);
        sprite->setTextColor(TFT_WHITE, TFT_GRAY);
        sprite->fillScreen(TFT_GRAY);
    }
}

//= End ofui_components.cpp ====================================================

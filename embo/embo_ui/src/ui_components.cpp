//==============================================================================
#include <ui_composer.h>
#include <ui_components.h>
#include <ui_event.h>

//==============================================================================
// Class Component implementation

//---------------------------------------------------------------------------------
// Constructor
Component::Component(Component* parent, Variant::Position position) : 
Subscriber(reinterpret_cast<ESPEventBroker<UIEvent>*>(parent ? parent->get_composer() : nullptr)),
parent(parent),  position(position) {
    if(parent) {
        this->zone = parent->zone;
        children.reserve(get_composer()->get_max_children());
        children.clear();
    }
}

//--------------------------------------------------------------------------------
void Component::add(Component* child) {
    if(child) {
        children.push_back(child);
    }
}

//--------------------------------------------------------------------------------
// remder
void Component::render() {
    for(auto child:children) {
        child->render();
    }
}

void Component::update() {
}

//--------------------------------------------------------------------------------
// invalidate
void Component::invalidate() {
    UIEvent event(UIEvent::EVT_UPDATE, this);
    get_composer()->event(event);
    for(auto child:children) {
        child->invalidate();
    }
}

//--------------------------------------------------------------------------------
// set_visible
void Component::set_visible(bool visible) {
    bool was_visible = this->visible;
    this->visible = visible;
    if(was_visible && !visible) {
        // Change visibility to invisible
        this->set_active(false);
    }
    else if(!was_visible && visible) {
        // Change visibility to visible
        this->set_active(true);
    }
}

//--------------------------------------------------------------------------------
Component* Component::is_within(const Variant::Point& pt, Variant::Point ref) {
    Component* c = nullptr;
    //Serial.printf("Finding component at point (%d, %d) relative to reference point (%d, %d)\n",
    //     pt.x, pt.y, ref.x, ref.y);
    if(!is_visible()) return c;
    Variant::Position pos = get_position();
    pos.p.x += ref.x;
    pos.p.y += ref.y;
    if(within(pt, position)) {
        //Serial.printf("Point (%d, %d) is within component at position (%d, %d) with size (%d, %d)\n", 
        //    pt.x, pt.y, position.p.x + ref.x, position.p.y + ref.y, position.s.w, position.s.h);
        c = this;
    }
    return c;
}

//--------------------------------------------------------------------------------
void Component::notify_hit(const Subscription& subscription, UIEvent& event, 
    Variant::Point ref) {
    if(!is_visible()) return;
    bool found = false;
    Variant::Point point = event.get_variant().get_point();
    Variant::Position pos = position;
    pos.p.x += ref.x;
    pos.p.y += ref.y;
    if(within(point, pos)) {
        event.set_source(this);
        if(subscription.get_flags() & UIComposer::SUB_SELF) {
            if(this == subscription.get_subscriber()) found = true;
        } else {
            found = true;
        }
    }
    if(found) {
        subscription.get_callback()(subscription, event);
    }
    ref.x += position.p.x;
    ref.y += position.p.y;
    for( auto child:children) {
        child->notify_hit(subscription, event, ref);
    }
}

//==============================================================================
void Zone::render() {
    Component::render();
}

//--------------------------------------------------------------------------------
void Zone::update() {
    Component::update();
    if (!this->is_visible()) return;
    if(sprite == nullptr) {
        sprite = new LGFX_Sprite(this->get_composer()->get_display());
        sprite->setPsram(true);
        sprite->setColorDepth(16);
        sprite->setPsram(true);
        sprite->createSprite(position.s.w, position.s.h);
    }
    if(children.empty()) {
        sprite->clear(bg_color);
        if (this->frame) {
            // Since it's relative to the sprite, top-left is ALWAYS (0,0)
            // sprite.width() and sprite.height() match your pre-allocated size
            sprite->drawRect(0, 0, sprite->width() - 1, sprite->height() - 1, frame_color);
        }
        sprite->pushSprite(get_composer()->get_display(), this->position.p.x, this->position.p.y);
        return;
    }
}

//==============================================================================
//------------------------------------------------------------------------------
void Text::render() {

}

//------------------------------------------------------------------------------
void Text::update() {
    LGFX_Sprite* sprite = this->zone->get_sprite();
    sprite->setCursor(position.p.x, position.p.y);
    sprite->fillRect(position.p.x, position.p.y, position.s.w, position.s.h, bg_color);
    sprite->setTextColor(text_color);
    sprite->setFont(&font);
    sprite->print(text);
    sprite->pushSprite(get_composer()->get_display(), 
        zone->get_position().p.x, zone->get_position().p.y);
}

//==============================================================================

//------------------------------------------------------------------------------
void Panel::set_visible(bool visible) {
    Component::set_visible(visible);
    for(auto child:children) {
        child->set_visible(visible);
    }
}

//-------------------------------------------------------------------------------
void Panel::update() {
    Component::update();
    if (!this->is_visible()) return;

    LGFX_Sprite* sprite = this->zone->get_sprite();
    sprite->fillRect(this->position.p.x, this->position.p.y, this->position.s.w, this->position.s.h, bg_color);
    sprite->pushSprite(get_composer()->get_display(), zone->get_position().p.x, zone->get_position().p.y);
}

//--------------------------------------------------------------------------------
void Panel::render() {
    Component::render();
}

//==============================================================================

//--------------------------------------------------------------------------------
void Button::render() {
    Component::render();
    UIComposer *composer = get_composer();
    Subscriber* subscriber = (Subscriber*)this;
    subscribe(Subscription(UIEvent::EVT_TOUCH_DOWN, this, 0, 
    [this](const Subscription& subscription, const UIEvent& event) {
        Button* button = static_cast<Button*>(event.get_source());
        UIEvent btn_event(UIEvent::EVT_PRESS, this);
        if(this == button)
            get_composer()->system_event(btn_event);
    }));
    subscribe(Subscription(UIEvent::EVT_TOUCH_UP, this, 0, 
    [this](const Subscription& subscription, const UIEvent& event) {
        Button* button = static_cast<Button*>(event.get_source());
        UIEvent btn_event(UIEvent::EVT_RELEASE, this);
        if(this == button)
            get_composer()->system_event(btn_event);
    }));
    subscribe(Subscription(UIEvent::EVT_TOUCH_DRAG, this, 0, 
    [this](const Subscription& subscription, const UIEvent& event) {
        Button* button = static_cast<Button*>(event.get_source());
    }));
}

//--------------------------------------------------------------------------------
void Button::update() {
    Component::update();
    if (!this->is_visible()) return;

    LGFX_Sprite* sprite = this->zone->get_sprite();

    if(button == nullptr) {
        button = new LGFX_Button();
        button->initButtonUL(
            sprite, 
            this->position.p.x, this->position.p.y, 
            this->position.s.w, this->position.s.h, 
            TFT_WHITE, bg_color, text_color, this->label);   

    }
    button->setLabelText(this->label);
    button->drawButton(false, this->label);
    sprite->pushSprite(get_composer()->get_display(), zone->get_position().p.x, zone->get_position().p.y);
}
//==============================================================================
// Icon implemntation
//------------------------------------------------------------------------------
void Icon::render() {
    Component::render();
    // No heavy processing here—the image is already decoded and waiting in the registry cache!
}

//------------------------------------------------------------------------------
void Icon::update() {
    Component::update();
    if(!this->is_visible()) return;

    if(sprite == nullptr) {
        load_from_fs();
    }

    if(sprite) {
        LGFX_Sprite* zone_sprite = this->zone->get_sprite();
        if(transparency == Icon::POINT) {
            transparent_color = sprite->readPixel(transparent_point.x, transparent_point.y);
        }
        if(transparency == Icon::NONE) {
            sprite->pushSprite(zone_sprite, position.p.x, this->position.p.y);  
        } else {
            sprite->pushSprite(zone_sprite, position.p.x, this->position.p.y, transparent_color);  
        }
        zone_sprite->pushSprite(zone->get_position().p.x, zone->get_position().p.y);
    }
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
// Private methods

bool Icon::load_from_fs() {
    fs::File file = LittleFS.open(file_path, "r");
    if (!file) {
        Serial.printf("Error: Could not open icon file: %s\n", file_path);
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

    // DIAGNOSTIC CHECK: Print exactly what values your bit-shifting produces
    Serial.printf("DEBUG: Parsed File Dimensions -> Width: %d, Height: %d\n", img_w, img_h);

    if (img_w <= 0 || img_h <= 0 || img_w > 480 || img_h > 480) { // Add safety thresholds
        Serial.printf("Error: Broken PNG dimensions calculated for %s\n", file_path);
        file.close();
        return 0;
    }

    if(sprite == nullptr) {
        sprite = new LGFX_Sprite(get_composer()->get_display());
        sprite->setColorDepth(16);
    }
    
    // Check if allocation actually succeeds
    if (sprite->createSprite(img_w, img_h) == nullptr) {
        Serial.printf("Error: Failed to allocate RAM for sprite %dx%d\n", img_w, img_h);
        delete sprite;
        sprite = nullptr;
        file.close();
        return false;
    }

    file.seek(0); 
    bool success = sprite->drawPng(&file, 0, 0);
    file.close(); 

    if (!success) {
        Serial.printf("Error: LovyanGFX decoder failed on file: %s\n", file_path);
        sprite->deleteSprite();
        delete sprite;
        sprite = nullptr;
        return false;
    }

    return true;
}


//= End ofui_components.cpp ====================================================

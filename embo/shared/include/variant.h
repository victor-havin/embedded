#pragma once

class Variant
{
public:
    enum Type : byte {
        NOTHING,
        POINT,
        SIZE,
        POSITION,
        CHARACTER,
        LAST
    };

    struct Point 
    {
        int16_t x;
        int16_t y;
        Point(int x = 0, int y = 0) : x(static_cast<int16_t>(x)), y(static_cast<int16_t>(y)) {}
    };

    struct Size
    {
        int16_t w;
        int16_t h;
        Size(int w = 0, int h = 0) : w(static_cast<int16_t>(w)), h(static_cast<int16_t>(h)) {}
    };

    struct Position
    {
        Point p;
        Size s;
    };

    struct Character
    {
        byte flags;
        char code;
    };

    union Data {
        Point point;
        Size size;
        Position position;
        Character character;
        Data() : position() {}
    };

    Variant():variant_type(NOTHING) {}
    Variant(Point point):variant_type(POINT) {
        data.point = point;
    }
    Variant(Character character):variant_type(CHARACTER) {
        data.character = character;
    }
    Point get_point() const {
        return data.point;
    }
    Size get_size() const {
        return data.size;
    }
    Position get_position() const {
        return data.position;
    }
    Character get_character() const {
        return data.character;
    }
    // ToDo: add more constructors and getters
private:
    Type variant_type;
    Data data;
};


static inline bool within(Variant::Point pt, Variant::Position pos) {
    return (pt.x > pos.p.x) && (pt.x < pos.p.x + pos.s.w) 
        && (pt.y > pos.p.y) && (pt.y < pos.p.y + pos.s.h);
}
#pragma once

class Variant
{
public:
    enum Type {
        NOTHING,
        POINT,
        SIZE,
        POSITION,
        CHARACTER,
        LAST
    };

    struct Point 
    {
        int x;
        int y;
    };

    struct Size
    {
        int w;
        int h;
    };

    struct Position
    {
        Point p;
        Size s;
    };

    struct Character
    {
        bool special;
        int code;
        char ch;
    };

    union Data {
        Point point;
        Size size;
        Position position;
        Character character;
    };

    Variant():variant_type(NOTHING) {}
    Variant(Point point):variant_type(POINT) {
        data.point = point;
    }
    Variant(Character character):variant_type(CHARACTER) {
        data.character = character;
    }
    Point get_point() const {
        //assert(variant_type == POINT); 
        return data.point;
    }
    Size get_size() const {
        return data.size;
    }
    Position get_position() const {
        return data.position;
    }
    Character get_character() const {
        //assert(variant_type == CHARACTER);
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
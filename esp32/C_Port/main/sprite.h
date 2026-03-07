
#pragma once
#include "gif.h"
#include "rgb565.h"
#include <cstdint>
#include <cstring>

struct Sprite {
    const uint16_t* pixels = nullptr;  // Points into gif.pixels() or R565Image
    GifSprite       gif;               // Inline — file data + RGB565 in PSRAM, struct itself is small
    R565Image       image;             // Owns memory if static
    uint16_t        w = 0, h = 0;
    int16_t         base_x = 0, base_y = 0;

    bool loaded() const { return pixels != nullptr; }
    bool is_gif() const { return gif.loaded(); }
};

inline bool load_sprite(Sprite& s, const char* path, int base_x, int base_y) {
    s.base_x = base_x;
    s.base_y = base_y;

    if (strstr(path, ".gif")) {
        if (!s.gif.load(path)) return false;
        s.w = s.gif.width();
        s.h = s.gif.height();
        s.pixels = s.gif.pixels();
        return true;
    }

    if (!s.image.load(path)) return false;
    s.w = s.image.width;
    s.h = s.image.height;
    s.pixels = s.image.pixels;
    return true;
}
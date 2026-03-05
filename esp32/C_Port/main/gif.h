#pragma once

#include "AnimatedGIF.h"
#include <cstdint>

class GifSprite {
public:
    GifSprite();
    ~GifSprite();

    // Load GIF file into PSRAM, parse dimensions, allocate RGB565 output
    bool load(const char* path);

    // Bind shared decoder to this sprite's data, restore frame position
    bool bind(AnimatedGIF& decoder);

    // Decode next frame, save position. Returns frame delay in ms.
    int next_frame(AnimatedGIF& decoder);

    void close();

    uint16_t* pixels() const { return _rgb565; }
    int width()  const { return _w; }
    int height() const { return _h; }
    bool loaded() const { return _rgb565 != nullptr; }

private:
    uint8_t*  _file_data = nullptr;  // GIF file in PSRAM
    int       _file_size = 0;
    uint16_t* _rgb565    = nullptr;  // RGB565 output buffer (PSRAM)
    int       _w = 0, _h = 0;
    int       _file_pos = 0;         // Saved decoder position between binds
};
#pragma once

#include "protocol.h"
#include "config.h"
#include "gif.h"
#include "rgb565.h"
#include "AnimatedGIF.h"
#include <cstdint>

// Flag bits in FlashDataHeader::flags
constexpr uint8_t FLAG_CPU_WARM      = 0x01;
constexpr uint8_t FLAG_CPU_HOT       = 0x02;
constexpr uint8_t FLAG_WEATHER_AVAIL = 0x04;
constexpr uint8_t FLAG_TRAIN0_AVAIL  = 0x08;
constexpr uint8_t FLAG_TRAIN1_AVAIL  = 0x10;

constexpr int NUM_WEATHER_ICONS = 7;

struct Sprite {
    const uint16_t* pixels = nullptr;  // Points into gif.pixels() or R565Image
    GifSprite       gif;               // Inline — file data + RGB565 in PSRAM, struct itself is small
    R565Image       image;             // Owns memory if static
    uint16_t        w = 0, h = 0;
    int16_t         base_x = 0, base_y = 0;

    bool loaded() const { return pixels != nullptr; }
    bool is_gif() const { return gif.loaded(); }
};

class FlashModeManager {
public:
    FlashModeManager();
    ~FlashModeManager();

    // Load config and all assets from flash. Returns false on failure.
    bool init(const char* config_path);

    // Network data handlers
    void update_from_data(const FlashDataHeader& data);

    // Get the stream layer buffer (for receiving dirty rects into)
    uint16_t* stream_pixels() { return _stream; }

    // Mark a region of the stream layer dirty
    void mark_stream_dirty(int x, int y, int w, int h);

    // Advance animations + bobbing, composite all layers into framebuf.
    // stride: distance in pixels between rows in the destination buffer.
    //   Use FRAME_WIDTH for a contiguous 240-wide buffer (legacy).
    //   Use DISP_STRIDE to composite directly into the DMA framebuffer.
    void tick_and_composite(uint16_t* framebuf, int stride = FRAME_WIDTH);

    bool is_loaded() const { return _loaded; }

private:
    FlashConfig _config;
    bool _loaded = false;

    // Shared GIF decoder — lives in internal SRAM (~24KB)
    AnimatedGIF _decoder;

    // Layers
    Sprite    _bg;
    uint16_t* _stream = nullptr;

    Sprite    _char_normal;
    Sprite    _char_warm;
    Sprite    _char_hot;

    Sprite    _weather[NUM_WEATHER_ICONS];

    // State
    int       _char_state = 0;
    int       _weather_index = -1;
    uint8_t   _flags = 0;

    // Bobbing
    bool      _bob_enabled = false;
    float     _bob_speed = 1.0f;
    float     _bob_amp = 5.0f;
    int64_t   _start_us = 0;

    // GIF animation timing (microseconds)
    int64_t   _bg_gif_next_us = 0;
    int64_t   _char_gif_next_us = 0;
    int64_t   _weather_gif_next_us = 0;

    // Helpers
    bool load_sprite(Sprite& s, const char* path, int base_x, int base_y);
    Sprite* active_char();
    Sprite* active_weather();

    void advance_animations();
    void composite_layer_opaque(uint16_t* dst, int dst_stride,
                                const uint16_t* src, int sw, int sh);
    void composite_layer_transparent(uint16_t* dst, int dst_stride,
                                     const uint16_t* src,
                                     int sw, int sh, int dx, int dy);
};
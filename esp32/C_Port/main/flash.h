#pragma once

#include "protocol.h"
#include "config.h"
#include "gif.h"
#include "rgb565.h"
#include "AnimatedGIF.h"
#include <cstdint>
#include <cstring>

// Flag bits in FlashDataHeader::flags
constexpr uint8_t FLAG_CPU_WARM      = 0x01;
constexpr uint8_t FLAG_CPU_HOT       = 0x02;
constexpr uint8_t FLAG_WEATHER_AVAIL = 0x04;
constexpr uint8_t FLAG_TRAIN0_AVAIL  = 0x08;
constexpr uint8_t FLAG_TRAIN1_AVAIL  = 0x10;

constexpr int NUM_WEATHER_ICONS = 7;

// Dirty row bitmask size (960 rows / 8 = 120 bytes)
constexpr int DIRTY_BITMASK_BYTES = (FRAME_HEIGHT + 7) / 8;

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

    // Call after writing dirty rects into stream_pixels().
    // Marks affected rows for recomposite.
    void mark_stream_rows_dirty(int y, int h);

    // Mark a region of the stream layer dirty (legacy alias)
    void mark_stream_dirty(int x, int y, int w, int h);

    // Advance animations + bobbing, composite only dirty rows into framebuf.
    // stride: distance in pixels between rows in the destination buffer.
    // Returns the number of rows actually composited (for perf monitoring).
    int tick_and_composite(uint16_t* framebuf, int stride = FRAME_WIDTH);

    bool is_loaded() const { return _loaded; }

private:
    FlashConfig _config;
    bool _loaded = false;

    // Shared GIF decoder in internal SRAM
    AnimatedGIF _decoder;

    // Layers
    Sprite    _bg;
    uint16_t* _stream = nullptr;
    bool      _stream_has_content = false;

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

    // --- Dirty row tracking ---
    // Per-row bitmask: only dirty rows are recomposited each frame.
    // After compositing, the DMA buffer retains correct content for clean rows.
    uint8_t   _dirty_rows[DIRTY_BITMASK_BYTES];
    bool      _force_full = true;  // First frame after init

    // Previous frame sprite positions — used to mark old bbox dirty when sprites move
    int       _prev_chr_x = 0, _prev_chr_y = 0;
    int       _prev_chr_w = 0, _prev_chr_h = 0;
    int       _prev_wth_x = 0, _prev_wth_y = 0;
    int       _prev_wth_w = 0, _prev_wth_h = 0;
    int       _prev_wth_index = -1;

    // Dirty helpers
    void mark_all_dirty();
    void mark_rows_dirty(int y, int h);

    // Helpers
    bool load_sprite(Sprite& s, const char* path, int base_x, int base_y);
    Sprite* active_char();
    Sprite* active_weather();

    void advance_animations();
};
#pragma once
#include <cstdint>

// Display dimensions (Qualia 240x960 bar display)
constexpr int FRAME_WIDTH  = 240;
constexpr int FRAME_HEIGHT = 960;
constexpr int FRAME_PIXELS = FRAME_WIDTH * FRAME_HEIGHT;
constexpr int FRAME_BYTES  = FRAME_PIXELS * 2;  // RGB565

// Protocol message types (must match PC sender)
namespace proto {
    constexpr uint8_t MSG_FULL_FRAME  = 0x00;
    constexpr uint8_t MSG_DIRTY_RECTS = 0x01;
    constexpr uint8_t MSG_NO_CHANGE   = 0x02;
    constexpr uint8_t MSG_FLASH_DATA  = 0x03;
    constexpr uint8_t MSG_RESET       = 0x04;
    constexpr uint8_t MSG_SET_MODE    = 0x05;
    constexpr uint8_t MSG_RECONNECT   = 0x06;
    constexpr uint8_t MSG_SET_BRIGHTNESS = 0x07;

    constexpr uint8_t MODE_STREAMING = 0x00;
    constexpr uint8_t MODE_FLASH     = 0x01;
}

constexpr int TCP_PORT = 8765;
constexpr uint16_t TRANSPARENT_COLOR = 0xF81F;  // Magenta

struct DirtyRect {
    uint16_t x, y, w, h;
};

struct __attribute__((packed)) DirtyRectsHeader {
    uint8_t rectCount;
    uint8_t colorMode;        // (0=RGB565, 1=RGB444, 2=RGB343, 3=RGB332)
    uint16_t rleEscapeColor;
};

enum class ColorMode : uint8_t {
    RGB565 = 0,
    RGB444 = 1,
    RGB343 = 2,
    RGB332 = 3,
};

// Flash mode data header — sent after MSG_FLASH_DATA message type byte.
// All multi-byte fields are little-endian.
// Sensor values are fixed-point: actual = raw / 10.0
struct __attribute__((packed)) FlashDataHeader {
    uint8_t  weather_index;      // Weather icon index (0-6), or 0xFF for none
    uint8_t  flags;              // FLAG_CPU_WARM | FLAG_CPU_HOT | FLAG_WEATHER_AVAIL | ...
    uint16_t cpu_percent_x10;    // CPU usage * 10
    uint16_t cpu_temp_x10;       // CPU temperature * 10
    uint16_t mem_percent_x10;    // Memory usage * 10
    int16_t  weather_temp_x10;   // Weather temperature * 10 (signed)
    uint16_t train0_mins_x10;    // Train 0 arrival * 10
    uint16_t train1_mins_x10;    // Train 1 arrival * 10
    uint8_t  rect_count;         // Number of dirty rects following
    uint8_t  color_mode;         // ColorMode for dirty rects (if rect_count > 0)
    uint16_t rle_escape_color;   // RLE escape color (if rect_count > 0)
};
// Total: 18 bytes

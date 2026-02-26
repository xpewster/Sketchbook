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
    uint16_t rleEscapeColor;  // if a pixel matches this color, the next two uint16_ts are a (color, count) pair instead of a literal pixel
};

enum class ColorMode : uint8_t {
    RGB565 = 0,
    RGB444 = 1,
    RGB343 = 2,
    RGB332 = 3,
};

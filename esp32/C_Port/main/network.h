#pragma once
#include "protocol.h"
#include <cstdint>
#include <cstddef>

// Result of recv_frame()
enum class RecvResult {
    OK,
    NO_CHANGE,
    MODE_CHANGED,
    RESET_REQUESTED,
    DISCONNECTED,
    ERROR,
};

// Initialize WiFi station mode (blocks until connected)
void wifi_init();

// Initialize network buffers
void network_init();
void network_cleanup();

// Start TCP server on TCP_PORT. Blocks, accepts one client at a time.
// Calls recv_callback for each received frame.
// framebuf: pointer to the LGFX internal framebuffer to write into.
void tcp_server_start(uint16_t* framebuf);

// Receive one protocol message from client socket.
// Writes pixels directly into framebuf.
RecvResult recv_frame(int sock, uint16_t* framebuf, uint8_t& current_mode);

class BitReader {
    const uint8_t* data;
    const uint8_t* end;
    uint32_t buf = 0;
    int bits = 0;

    void refill() {
        while (bits <= 24 && data < end) {
            buf |= (uint32_t)(*data++) << (24 - bits);
            bits += 8;
        }
    }
public:
    BitReader(const uint8_t* d, size_t len) : data(d), end(d + len) { refill(); }

    uint16_t read(int nbits) {
        if (bits < nbits) refill();
        uint16_t val = (buf >> (32 - nbits)) & ((1u << nbits) - 1);
        buf <<= nbits;
        bits -= nbits;
        return val;
    }

    bool exhausted() const { return data >= end && bits <= 0; }
};

constexpr int bitsPerPixel(ColorMode mode) {
    switch (mode) {
        case ColorMode::RGB565: return 16;
        case ColorMode::RGB444: return 12;
        case ColorMode::RGB343: return 10;
        case ColorMode::RGB332: return 8;
    }
    return 16; // default to RGB565
}

// Count is also N-bit, so max run is limited by color depth
constexpr uint16_t maxRunLength(ColorMode mode) {
    return (1 << bitsPerPixel(mode)) - 1; // 65535, 4095, 1023, 255
}

constexpr uint16_t minRleRunLength() { return 4; } // same for all modes

// Packed byte size for N pixels
inline size_t packedByteSize(size_t nPixels, ColorMode mode) {
    return (nPixels * bitsPerPixel(mode) + 7) / 8;
}

static uint16_t rgb332_to_565[256];
inline void init_color_luts() {
    for (int i = 0; i < 256; i++) {
        uint8_t r3 = (i >> 5) & 7, g3 = (i >> 2) & 7, b2 = i & 3;
        rgb332_to_565[i] = (((r3 << 2) | (r3 >> 1)) << 11) |
                           (((g3 << 3) | g3) << 5) |
                           ((b2 << 3) | (b2 << 1) | (b2 >> 1));
    }
}

inline uint16_t toRGB565(uint16_t px, ColorMode mode) {
    switch (mode) {
    case ColorMode::RGB565: return px;
    case ColorMode::RGB332: return rgb332_to_565[px & 0xFF];
    case ColorMode::RGB444: {
        uint8_t r = (px >> 8) & 0xF, g = (px >> 4) & 0xF, b = px & 0xF;
        return (((r << 1) | (r >> 3)) << 11) | (((g << 2) | (g >> 2)) << 5) | ((b << 1) | (b >> 3));
    }
    case ColorMode::RGB343: {
        uint8_t r = (px >> 7) & 7, g = (px >> 3) & 0xF, b = px & 7;
        return (((r << 2) | (r >> 1)) << 11) | (((g << 2) | (g >> 2)) << 5) | ((b << 2) | (b >> 1));
    }
    }
    return px;
}

// Unified pixel writer helper
inline void writePixel(uint16_t* framebuf, const DirtyRect& r,
                       int& px_x, int& px_y, uint16_t rgb565val) {
    framebuf[(r.y + px_y) * FRAME_WIDTH + (r.x + px_x)] = rgb565val;
    if (++px_x >= r.w) { px_x = 0; px_y++; }
}

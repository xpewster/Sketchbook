#pragma once
#include "protocol.h"
#include <cstdint>
#include <cstddef>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

// Result of recv_frame()
enum class RecvResult {
    OK,
    NO_CHANGE,
    MODE_CHANGED,
    RESET_REQUESTED,
    RECONNECT_REQUESTED,
    TIMEOUT,        // Socket timeout — no data available (not an error)
    DISCONNECTED,
    ERROR,
    NO_OP_AFTER, // nothing to be done after
};

// Initialize WiFi station mode
void wifi_init();

// Disconnect and reconnect WiFi
void reconnect_wifi();

// Initialize network buffers
void network_init();
void network_cleanup();

// Initialize SNTP server for time sync
void time_sync_init();

// Pre-load flash mode assets based on saved NVS mode.
// Returns true if flash assets loaded successfully.
bool preload_flash_assets();

// --- Idle GIF ---

// Load the idle GIF based on saved NVS mode, render first frame, start animation task.
// dma_visible: pointer into DMA framebuffer at the visible area offset.
void idle_gif_load_and_show(uint16_t* dma_visible);

// Start/stop idle GIF animation
void idle_gif_start();
void idle_gif_stop();

// Suspend/resume the idle task entirely (reduces PSRAM bus contention
// during heavy operations like flash asset loading)
void idle_gif_freeze(bool frozen);

// Start TCP server on TCP_PORT. Blocks, accepts one client at a time.
// framebuf: pointer to the recv/composite buffer.
void tcp_server_start(uint16_t* framebuf);

// Receive one protocol message from client socket.
// Writes pixels directly into framebuf (streaming) or flash stream layer (flash mode).
RecvResult recv_frame(int sock, uint16_t* framebuf, uint8_t& current_mode);

// After recv_frame returns OK with MSG_FLASH_DATA, this holds the parsed header.
extern FlashDataHeader g_last_flash_data;

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
    return 16;
}

constexpr uint16_t maxRunLength(ColorMode mode) {
    return (1 << bitsPerPixel(mode)) - 1;
}

constexpr uint16_t minRleRunLength() { return 4; }

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

inline void writePixel(uint16_t* framebuf, const DirtyRect& r,
                       int& px_x, int& px_y, uint16_t rgb565val) {
    framebuf[(r.y + px_y) * FRAME_WIDTH + (r.x + px_x)] = rgb565val;
    if (++px_x >= r.w) { px_x = 0; px_y++; }
}
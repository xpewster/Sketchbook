#pragma once

#include <cstdint>

// ============================================================
// Post-Processing Pipeline
// ============================================================
// Generic per-pixel post-processing applied when writing to the DMA
// framebuffer. All DMA writes go through pp_pixel().
//
// Currently supports:
//   - Brightness (0-255). Uses per-channel LUTs (128 bytes
//     total in L1 cache). Combo of linear LUT + photoshop-like curve to maintain saturated colors at low brightness.

// --- Channel LUTs (128 bytes total, L1 cache resident) ---
extern uint8_t pp_r_lut[32];   // 5-bit red   (0-31 → 0-31)
extern uint8_t pp_g_lut[64];   // 6-bit green (0-63 → 0-63)
extern uint8_t pp_b_lut[32];   // 5-bit blue  (0-31 → 0-31)
extern uint8_t pp_brightness;  // Current brightness (0-255, 255 = full)

/// Set brightness (0=black, 255=full). Recomputes LUTs.
void pp_set_brightness(uint8_t brightness);

uint8_t pp_get_brightness();

/// Initialize post-processing (sets LUTs to identity).
void pp_init(uint8_t brightness = 255);

/// Process one RGB565 pixel: apply brightness + endianness byte-swap for DMA.
/// This replaces SWAP16() everywhere we write to the DMA buffer.
static inline __attribute__((always_inline))
uint16_t pp_pixel(uint16_t px) {
    // Fast path: full brightness — just swap bytes
    if (__builtin_expect(pp_brightness == 255, 1)) {
        return __builtin_bswap16(px);
    }

    uint16_t r = pp_r_lut[(px >> 11) & 0x1F];
    uint16_t g = pp_g_lut[(px >> 5)  & 0x3F];
    uint16_t b = pp_b_lut[ px        & 0x1F];

    return __builtin_bswap16((r << 11) | (g << 5) | b);
}
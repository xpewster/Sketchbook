#include "postprocess.h"

static const char* TAG = "postprocess";

// LUT storage — initialized to identity (brightness 255)
uint8_t pp_r_lut[32];
uint8_t pp_g_lut[64];
uint8_t pp_b_lut[32];
uint8_t pp_brightness = 255;

// Called once at startup and whenever brightness changes
static void recompute_luts() {
    uint16_t b = pp_brightness;  // Wider type to avoid overflow in multiply

    for (int i = 0; i < 32; i++) {
        pp_r_lut[i] = (uint8_t)((i * b + 127) / 255);  // +127 for rounding
    }
    for (int i = 0; i < 64; i++) {
        pp_g_lut[i] = (uint8_t)((i * b + 127) / 255);
    }
    for (int i = 0; i < 32; i++) {
        pp_b_lut[i] = (uint8_t)((i * b + 127) / 255);
    }
}

void pp_set_brightness(uint8_t brightness) {
    if (brightness == pp_brightness) return;
    pp_brightness = brightness;
    recompute_luts();
}

// Ensure LUTs start as identity
__attribute__((constructor))
static void pp_init() {
    pp_brightness = 255;
    recompute_luts();
}
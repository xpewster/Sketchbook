#include "postprocess.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "postprocess";

uint8_t pp_r_lut[32];
uint8_t pp_g_lut[64];
uint8_t pp_b_lut[32];
uint8_t pp_brightness = 255;

static constexpr float GAMMA = 2.2f; // Display gamma
static constexpr float INV_GAMMA = 1.0f / 2.2f;
static constexpr float BOOST_K = 0.274f;

// Brightness method 0: perceptually linear
static void build_channel_lut_0(uint8_t* lut, int max_val, float brightness_linear) {
    for (int i = 0; i <= max_val; i++) {
        float linear = powf((float)i / max_val, GAMMA);
        linear *= brightness_linear;
        float out = powf(linear, INV_GAMMA) * max_val;
        int val = (int)(out + 0.5f);
        lut[i] = (uint8_t)(val > max_val ? max_val : val);
    }
}

// Brightness method 1: photoshop like curve
static void build_channel_lut_1(uint8_t* lut, int max_val, float a_base, float a_boost) {
    for (int i = 0; i <= max_val; i++) {
        float x = (float)i / max_val;
        float x2 = x * x;
        float x4 = x2 * x2;
        float a_eff = a_base + a_boost * x4;

        float y;
        if (a_eff < 0.001f) {
            y = 0.0f;  // Near-zero exponent → all black
        } else {
            y = 1.0f - powf(1.0f - x, a_eff);
        }

        int val = (int)(y * max_val + 0.5f);
        // uint8_t out = (uint8_t)(val > max_val ? max_val : (val < 0 ? 0 : val));

        // Weight with existing linear LUT to reduce harshness of curve
        float m = 1.0f - a_base;
        float m2 = m * m;
        float m4 = m2 * m2;
        float m8 = m4 * m4;
        uint8_t out = (uint8_t)((val > max_val ? max_val : (val < 0 ? 0 : val)) * (1.0f - m8));
        lut[i] = (lut[i] + out) / 2;
    }
}

static void recompute_luts() {
    float brightness_linear = pp_brightness / 255.0f;
    float a_boost = (1.0f - brightness_linear) * BOOST_K;

    build_channel_lut_0(pp_r_lut, 31, brightness_linear);
    build_channel_lut_0(pp_g_lut, 63, brightness_linear);
    build_channel_lut_0(pp_b_lut, 31, brightness_linear);
    build_channel_lut_1(pp_r_lut, 31, brightness_linear, a_boost);
    build_channel_lut_1(pp_g_lut, 63, brightness_linear, a_boost);
    build_channel_lut_1(pp_b_lut, 31, brightness_linear, a_boost);
}

void pp_set_brightness(uint8_t brightness) {
    if (brightness == pp_brightness) return;
    pp_brightness = brightness;
    recompute_luts();
}

uint8_t pp_get_brightness() {
    return pp_brightness;
}

void pp_init(uint8_t brightness) {
    pp_brightness = brightness;
    recompute_luts();
}
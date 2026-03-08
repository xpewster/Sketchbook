#pragma once

#include "protocol.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

// ============================================================
// NVS Mode Persistence
// ============================================================
// Saves the last active mode (streaming/flash) so we can pick
// the right loading GIF on startup before WiFi/TCP connects.
// Matches CircuitPython's NVM-based mode persistence.

static constexpr const char* NVS_NAMESPACE = "sketchbook";
static constexpr const char* NVS_KEY_MODE  = "mode";
static constexpr const char* NVS_KEY_BRIGHTNESS = "brightness";

inline uint8_t load_saved_mode() {
    nvs_handle_t handle;
    uint8_t mode = proto::MODE_STREAMING;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, NVS_KEY_MODE, &mode);
        nvs_close(handle);
        ESP_LOGI("nvs", "Loaded mode %d", mode);
    }
    if (mode != proto::MODE_STREAMING && mode != proto::MODE_FLASH) {
        mode = proto::MODE_STREAMING;
    }
    return mode;
}

inline void save_mode(uint8_t mode) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_MODE, mode);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI("nvs", "Saved mode %d", mode);
    }
}

inline uint8_t load_saved_brightness() {
    nvs_handle_t handle;
    uint8_t brightness = 255;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &brightness);
        nvs_close(handle);
        ESP_LOGI("nvs", "Loaded brightness %d", brightness);
    }
    return brightness;
}

inline void save_brightness(uint8_t brightness) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, brightness);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI("nvs", "Saved brightness %d", brightness);
    }
}
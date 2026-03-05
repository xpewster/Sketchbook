#include "flash.h"
#include "storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

static const char* TAG = "flash";

// ============================================================
// Construction / Destruction
// ============================================================

FlashModeManager::FlashModeManager() {
    _start_us = esp_timer_get_time();
}

FlashModeManager::~FlashModeManager() {
    // GifSprite destructors handle their own PSRAM cleanup
    if (_stream) heap_caps_free(_stream);
}

// ============================================================
// Init
// ============================================================

bool FlashModeManager::init(const char* config_path) {
    if (!storage_available()) {
        ESP_LOGW(TAG, "Filesystem not available");
        return false;
    }

    if (!_config.load(config_path)) {
        ESP_LOGE(TAG, "No config at %s", config_path);
        return false;
    }

    ESP_LOGI(TAG, "Free PSRAM: %lu KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "Free internal RAM: %lu KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);

    // Allocate stream overlay (filled with magenta = transparent)
    _stream = (uint16_t*)heap_caps_malloc(FRAME_PIXELS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_stream) {
        ESP_LOGE(TAG, "Failed to allocate stream buffer");
        return false;
    }
    for (int i = 0; i < FRAME_PIXELS; i++) {
        _stream[i] = TRANSPARENT_COLOR;
    }

    const char* dir = FLASH_ASSETS_DIR;

    // --- Background ---
    if (_config.get_bool("bg_enabled")) {
        const char* bg_file = _config.get_str("bg_file", "");
        if (bg_file[0]) {
            char path[128];
            snprintf(path, sizeof(path), "%s/%s", dir, bg_file);
            load_sprite(_bg, path, 0, 0);
            ESP_LOGI(TAG, "Background: %s", _bg.loaded() ? "OK" : "FAILED");
        }
    }

    // --- Character sprites ---
    if (_config.get_bool("char_enabled")) {
        int cx = _config.get_int("char_x", 0);
        int cy = _config.get_int("char_y", 0);

        auto load_char = [&](const char* key, Sprite& s) {
            const char* f = _config.get_str(key, "");
            if (f[0]) {
                char path[128];
                snprintf(path, sizeof(path), "%s/%s", dir, f);
                load_sprite(s, path, cx, cy);
            }
        };

        load_char("char_file", _char_normal);
        if (_config.get_bool("char_has_warm"))
            load_char("char_warm_file", _char_warm);
        if (_config.get_bool("char_has_hot"))
            load_char("char_hot_file", _char_hot);

        _bob_enabled = _config.get_bool("char_bob");
        _bob_speed = _config.get_float("char_bob_speed", 1.0f);
        _bob_amp = _config.get_float("char_bob_amp", 5.0f);

        ESP_LOGI(TAG, "Character: normal=%s warm=%s hot=%s bob=%s",
                 _char_normal.loaded() ? "OK" : "NONE",
                 _char_warm.loaded() ? "OK" : "NONE",
                 _char_hot.loaded() ? "OK" : "NONE",
                 _bob_enabled ? "ON" : "OFF");
    }

    // --- Weather icons ---
    if (_config.get_bool("weather_enabled")) {
        static const char* names[] = {
            "sunny", "cloudy", "rainy", "thunderstorm", "foggy", "windy", "night"
        };

        int wx = _config.get_int("weather_x", 0);
        int wy = _config.get_int("weather_y", 0);
        int loaded = 0;

        for (int i = 0; i < NUM_WEATHER_ICONS; i++) {
            char key[48];
            snprintf(key, sizeof(key), "weather_%s_file", names[i]);
            const char* filename = _config.get_str(key, "");

            char path[128];
            if (filename[0]) {
                snprintf(path, sizeof(path), "%s/%s", dir, filename);
            } else {
                snprintf(path, sizeof(path), "%s/weather_%s.r565", dir, names[i]);
            }

            struct stat st;
            if (stat(path, &st) != 0) continue;

            if (load_sprite(_weather[i], path, wx, wy))
                loaded++;
        }
        ESP_LOGI(TAG, "Weather icons: %d loaded", loaded);
    }

    // Decode first frame for all loaded GIF sprites
    auto decode_first = [&](Sprite& s) {
        if (s.is_gif()) {
            s.gif.bind(_decoder);
            s.gif.next_frame(_decoder);
            s.pixels = s.gif.pixels();
        }
    };
    decode_first(_bg);
    decode_first(_char_normal);
    decode_first(_char_warm);
    decode_first(_char_hot);
    for (int i = 0; i < NUM_WEATHER_ICONS; i++) {
        decode_first(_weather[i]);
    }

    _loaded = true;
    ESP_LOGI(TAG, "Flash mode initialized. Free PSRAM: %lu KB",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "Free internal RAM: %lu KB",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    return true;
}

// ============================================================
// Asset Loading
// ============================================================

bool FlashModeManager::load_sprite(Sprite& s, const char* path, int base_x, int base_y) {
    s.base_x = base_x;
    s.base_y = base_y;

    if (strstr(path, ".gif")) {
        if (!s.gif.load(path)) return false;
        s.w = s.gif.width();
        s.h = s.gif.height();
        s.pixels = s.gif.pixels();
        return true;
    }

    // Static R565 image
    if (!s.image.load(path)) return false;
    s.w = s.image.width;
    s.h = s.image.height;
    s.pixels = s.image.pixels;
    return true;
}

// ============================================================
// State Updates from Network
// ============================================================

void FlashModeManager::update_from_data(const FlashDataHeader& data) {
    _flags = data.flags;

    int new_state = 0;
    if (data.flags & FLAG_CPU_HOT) new_state = 2;
    else if (data.flags & FLAG_CPU_WARM) new_state = 1;
    _char_state = new_state;

    if (data.flags & FLAG_WEATHER_AVAIL) {
        _weather_index = data.weather_index;
        if (_weather_index >= NUM_WEATHER_ICONS) _weather_index = -1;
    } else {
        _weather_index = -1;
    }
}

void FlashModeManager::mark_stream_dirty(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}

// ============================================================
// Active Sprite Selection
// ============================================================

Sprite* FlashModeManager::active_char() {
    switch (_char_state) {
        case 2:
            if (_char_hot.loaded()) return &_char_hot;
            if (_char_warm.loaded()) return &_char_warm;
            return _char_normal.loaded() ? &_char_normal : nullptr;
        case 1:
            if (_char_warm.loaded()) return &_char_warm;
            return _char_normal.loaded() ? &_char_normal : nullptr;
        default:
            return _char_normal.loaded() ? &_char_normal : nullptr;
    }
}

Sprite* FlashModeManager::active_weather() {
    if (_weather_index < 0 || _weather_index >= NUM_WEATHER_ICONS) return nullptr;
    return _weather[_weather_index].loaded() ? &_weather[_weather_index] : nullptr;
}

// ============================================================
// Animation
// ============================================================

void FlashModeManager::advance_animations() {
    int64_t now = esp_timer_get_time();

    // Background GIF
    if (_bg.is_gif() && now >= _bg_gif_next_us) {
        _bg.gif.bind(_decoder);
        int delay_ms = _bg.gif.next_frame(_decoder);
        _bg.pixels = _bg.gif.pixels();
        _bg_gif_next_us = now + (int64_t)delay_ms * 1000;
    }

    // Character GIF (active variant only)
    Sprite* chr = active_char();
    if (chr && chr->is_gif() && now >= _char_gif_next_us) {
        chr->gif.bind(_decoder);
        int delay_ms = chr->gif.next_frame(_decoder);
        chr->pixels = chr->gif.pixels();
        _char_gif_next_us = now + (int64_t)delay_ms * 1000;
    }

    // Weather GIF (active icon only)
    Sprite* wth = active_weather();
    if (wth && wth->is_gif() && now >= _weather_gif_next_us) {
        wth->gif.bind(_decoder);
        int delay_ms = wth->gif.next_frame(_decoder);
        wth->pixels = wth->gif.pixels();
        _weather_gif_next_us = now + (int64_t)delay_ms * 1000;
    }
}

// ============================================================
// Compositing
// ============================================================

void FlashModeManager::tick_and_composite(uint16_t* framebuf) {
    advance_animations();

    // Layer 1: Background
    if (_bg.loaded()) {
        composite_layer_opaque(framebuf, _bg.pixels, _bg.w, _bg.h);
    } else {
        memset(framebuf, 0, FRAME_PIXELS * 2);
    }

    // Layer 2: Stream overlay (magenta = transparent)
    composite_layer_transparent(framebuf, _stream, FRAME_WIDTH, FRAME_HEIGHT, 0, 0);

    // Layer 3: Weather icon
    Sprite* wth = active_weather();
    if (wth && wth->pixels) {
        composite_layer_transparent(framebuf, wth->pixels,
                                    wth->w, wth->h, wth->base_x, wth->base_y);
    }

    // Layer 4: Character (with bobbing offset on X axis)
    Sprite* chr = active_char();
    if (chr && chr->pixels) {
        int bob_offset = 0;
        if (_bob_enabled) {
            int64_t now = esp_timer_get_time();
            float elapsed_s = (float)(now - _start_us) / 1000000.0f;
            bob_offset = (int)(sinf(elapsed_s * _bob_speed * 2.0f * M_PI) * _bob_amp);
        }
        composite_layer_transparent(framebuf, chr->pixels,
                                    chr->w, chr->h,
                                    chr->base_x + bob_offset, chr->base_y);
    }
}

void FlashModeManager::composite_layer_opaque(uint16_t* dst, const uint16_t* src,
                                               int sw, int sh) {
    int cw = (sw < FRAME_WIDTH) ? sw : FRAME_WIDTH;
    int ch = (sh < FRAME_HEIGHT) ? sh : FRAME_HEIGHT;

    if (cw == FRAME_WIDTH && sw == FRAME_WIDTH) {
        memcpy(dst, src, (size_t)cw * ch * 2);
    } else {
        for (int y = 0; y < ch; y++) {
            memcpy(&dst[y * FRAME_WIDTH], &src[y * sw], cw * 2);
        }
    }
}

void FlashModeManager::composite_layer_transparent(uint16_t* dst, const uint16_t* src,
                                                     int sw, int sh, int dx, int dy) {
    int src_x0 = 0, src_y0 = 0;
    if (dx < 0) { src_x0 = -dx; dx = 0; }
    if (dy < 0) { src_y0 = -dy; dy = 0; }

    int draw_w = sw - src_x0;
    int draw_h = sh - src_y0;
    if (dx + draw_w > FRAME_WIDTH)  draw_w = FRAME_WIDTH - dx;
    if (dy + draw_h > FRAME_HEIGHT) draw_h = FRAME_HEIGHT - dy;

    if (draw_w <= 0 || draw_h <= 0) return;

    for (int y = 0; y < draw_h; y++) {
        const uint16_t* srow = &src[(src_y0 + y) * sw + src_x0];
        uint16_t* drow = &dst[(dy + y) * FRAME_WIDTH + dx];

        for (int x = 0; x < draw_w; x++) {
            uint16_t px = srow[x];
            if (px != TRANSPARENT_COLOR) {
                drow[x] = px;
            }
        }
    }
}
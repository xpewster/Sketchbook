#include "flash.h"

#include "storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

static const char* TAG = "flash";

// Byte-swap RGB565 for DMA buffer.
// Our pixel data is little-endian (native ESP32), but the LCD_CAM parallel
// interface shifts out bytes in memory order. LovyanGFX's pushImage handled
// this via setSwapBytes(true); since we write directly, we swap on write.
static inline uint16_t SWAP16(uint16_t v) { return __builtin_bswap16(v); }

// ============================================================
// Composite Parameters — passed to both cores
// ============================================================

struct CompositeParams {
    uint16_t*       dst;
    int             dst_stride;
    int             y_start;       // inclusive
    int             y_end;         // exclusive

    const uint8_t*  dirty_rows;    // Bitmask — skip clean rows

    const uint16_t* bg;
    int             bg_w, bg_h;

    const uint16_t* stream;
    bool            stream_active;

    const uint16_t* wth;
    int             wth_w, wth_h, wth_x, wth_y;

    const uint16_t* chr;
    int             chr_w, chr_h, chr_x, chr_y;
};

// ============================================================
// Transparent Row Composite
// ============================================================

static void composite_row_transparent(uint16_t* __restrict dst,
                                       const uint16_t* __restrict src,
                                       int count) {
    constexpr uint32_t TRANS_PAIR = ((uint32_t)TRANSPARENT_COLOR << 16) | TRANSPARENT_COLOR;
    int x = 0;

    // Align to 32-bit
    if (((uintptr_t)&src[x] & 2) && x < count) {
        uint16_t px = src[x];
        if (px != TRANSPARENT_COLOR) dst[x] = SWAP16(px);
        x++;
    }

    // Paired 32-bit reads — skip two transparent pixels with one branch
    for (; x + 1 < count; x += 2) {
        uint32_t pair = *(const uint32_t*)&src[x];
        if (pair == TRANS_PAIR) continue;
        uint16_t px0 = (uint16_t)(pair);
        uint16_t px1 = (uint16_t)(pair >> 16);
        if (px0 != TRANSPARENT_COLOR) dst[x]     = SWAP16(px0);
        if (px1 != TRANSPARENT_COLOR) dst[x + 1] = SWAP16(px1);
    }

    if (x < count) {
        uint16_t px = src[x];
        if (px != TRANSPARENT_COLOR) dst[x] = SWAP16(px);
    }
}

// ============================================================
// Core Composite: process dirty rows in [y_start, y_end)
// ============================================================

static inline bool is_row_dirty(const uint8_t* bitmask, int y) {
    return bitmask[y >> 3] & (1 << (y & 7));
}

static void composite_layers_range(const CompositeParams& p) {
    for (int y = p.y_start; y < p.y_end; y++) {

        // Skip clean rows — DMA buffer still has last frame's correct content
        if (!is_row_dirty(p.dirty_rows, y)) continue;

        uint16_t* drow = &p.dst[y * p.dst_stride];

        // --- Base layer: background + stream fused ---
        // All writes to drow use SWAP16() for DMA byte order.
        if (p.bg && y < p.bg_h) {
            const uint16_t* bg_row = &p.bg[y * p.bg_w];

            if (p.stream_active && p.stream) {
                const uint16_t* st_row = &p.stream[y * FRAME_WIDTH];
                int w = (p.bg_w < FRAME_WIDTH) ? p.bg_w : FRAME_WIDTH;

                int x = 0;
                constexpr uint32_t TRANS_PAIR =
                    ((uint32_t)TRANSPARENT_COLOR << 16) | TRANSPARENT_COLOR;

                if (((uintptr_t)&st_row[x] & 2) && x < w) {
                    uint16_t s = st_row[x];
                    drow[x] = SWAP16((s != TRANSPARENT_COLOR) ? s : bg_row[x]);
                    x++;
                }

                for (; x + 1 < w; x += 2) {
                    uint32_t spair = *(const uint32_t*)&st_row[x];
                    if (spair == TRANS_PAIR) {
                        // Both stream pixels transparent — copy bg pair (swapped)
                        drow[x]     = SWAP16(bg_row[x]);
                        drow[x + 1] = SWAP16(bg_row[x + 1]);
                    } else {
                        uint16_t s0 = (uint16_t)(spair);
                        uint16_t s1 = (uint16_t)(spair >> 16);
                        drow[x]     = SWAP16((s0 != TRANSPARENT_COLOR) ? s0 : bg_row[x]);
                        drow[x + 1] = SWAP16((s1 != TRANSPARENT_COLOR) ? s1 : bg_row[x + 1]);
                    }
                }

                if (x < w) {
                    uint16_t s = st_row[x];
                    drow[x] = SWAP16((s != TRANSPARENT_COLOR) ? s : bg_row[x]);
                }
            } else {
                // No stream — copy bg row with byte swap
                int cw = (p.bg_w < FRAME_WIDTH) ? p.bg_w : FRAME_WIDTH;
                for (int x = 0; x < cw; x++) {
                    drow[x] = SWAP16(bg_row[x]);
                }
            }
        } else {
            if (p.stream_active && p.stream) {
                const uint16_t* st_row = &p.stream[y * FRAME_WIDTH];
                for (int x = 0; x < FRAME_WIDTH; x++) {
                    uint16_t s = st_row[x];
                    drow[x] = (s != TRANSPARENT_COLOR) ? SWAP16(s) : 0;
                }
            } else {
                memset(drow, 0, FRAME_WIDTH * 2);
            }
        }

        // --- Weather overlay ---
        if (p.wth) {
            int wy0 = p.wth_y, wy1 = p.wth_y + p.wth_h;
            if (y >= wy0 && y < wy1) {
                int sy = y - wy0;
                int sx0 = 0, dx = p.wth_x;
                if (dx < 0) { sx0 = -dx; dx = 0; }
                int draw_w = p.wth_w - sx0;
                if (dx + draw_w > FRAME_WIDTH) draw_w = FRAME_WIDTH - dx;
                if (draw_w > 0) {
                    composite_row_transparent(
                        &drow[dx], &p.wth[sy * p.wth_w + sx0], draw_w);
                }
            }
        }

        // --- Character overlay ---
        if (p.chr) {
            int cy0 = p.chr_y, cy1 = p.chr_y + p.chr_h;
            if (y >= cy0 && y < cy1) {
                int sy = y - cy0;
                int sx0 = 0, dx = p.chr_x;
                if (dx < 0) { sx0 = -dx; dx = 0; }
                int draw_w = p.chr_w - sx0;
                if (dx + draw_w > FRAME_WIDTH) draw_w = FRAME_WIDTH - dx;
                if (draw_w > 0) {
                    composite_row_transparent(
                        &drow[dx], &p.chr[sy * p.chr_w + sx0], draw_w);
                }
            }
        }
    }
}

// ============================================================
// Dual-Core Helper Task (runs on Core 1)
// ============================================================

static TaskHandle_t      s_helper_task = nullptr;
static SemaphoreHandle_t s_helper_done = nullptr;
static CompositeParams   s_helper_params;

static void composite_helper_task(void*) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        composite_layers_range(s_helper_params);
        xSemaphoreGive(s_helper_done);
    }
}

// ============================================================
// Construction / Destruction
// ============================================================

FlashModeManager::FlashModeManager() {
    _start_us = esp_timer_get_time();
    mark_all_dirty();
}

FlashModeManager::~FlashModeManager() {
    if (_stream) heap_caps_free(_stream);
}

// ============================================================
// Dirty Row Tracking
// ============================================================

void FlashModeManager::mark_all_dirty() {
    memset(_dirty_rows, 0xFF, DIRTY_BITMASK_BYTES);
}

void FlashModeManager::mark_rows_dirty(int y, int h) {
    if (h <= 0) return;
    int y0 = (y < 0) ? 0 : y;
    int y1 = y + h;
    if (y1 > FRAME_HEIGHT) y1 = FRAME_HEIGHT;
    for (int row = y0; row < y1; row++) {
        _dirty_rows[row >> 3] |= (1 << (row & 7));
    }
}

void FlashModeManager::mark_stream_rows_dirty(int y, int h) {
    _stream_has_content = true;
    mark_rows_dirty(y, h);
}

void FlashModeManager::mark_stream_dirty(int x, int y, int w, int h) {
    (void)x; (void)w;
    mark_stream_rows_dirty(y, h);
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
    _stream_has_content = false;

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

    // Create dual-core composite helper (once)
    if (!s_helper_task) {
        s_helper_done = xSemaphoreCreateBinary();
        xTaskCreatePinnedToCore(
            composite_helper_task,
            "comp1",
            4096,
            nullptr,
            5,
            &s_helper_task,
            1              // Core 1 (network task is on Core 0)
        );
        ESP_LOGI(TAG, "Composite helper task created on Core 1");
    }

    // First frame must composite everything
    _force_full = true;
    mark_all_dirty();

    _loaded = true;
    ESP_LOGI(TAG, "Flash mode initialized. Free PSRAM: %lu KB",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "Free internal RAM: %lu KB",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
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

    // Character variant changed — mark old + new bboxes dirty
    if (new_state != _char_state) {
        Sprite* old_chr = active_char();
        if (old_chr) mark_rows_dirty(old_chr->base_y, old_chr->h);
        _char_state = new_state;
        Sprite* new_chr = active_char();
        if (new_chr) mark_rows_dirty(new_chr->base_y, new_chr->h);
    }

    int old_weather = _weather_index;
    if (data.flags & FLAG_WEATHER_AVAIL) {
        _weather_index = data.weather_index;
        if (_weather_index >= NUM_WEATHER_ICONS) _weather_index = -1;
    } else {
        _weather_index = -1;
    }

    // Weather icon changed — mark old + new bboxes dirty
    if (_weather_index != old_weather) {
        if (old_weather >= 0 && old_weather < NUM_WEATHER_ICONS && _weather[old_weather].loaded())
            mark_rows_dirty(_weather[old_weather].base_y, _weather[old_weather].h);
        Sprite* wth = active_weather();
        if (wth)
            mark_rows_dirty(wth->base_y, wth->h);
    }
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
// Animation (sets dirty flags when frames advance)
// ============================================================

void FlashModeManager::advance_animations() {
    int64_t now = esp_timer_get_time();

    // Background GIF
    if (_bg.is_gif() && now >= _bg_gif_next_us) {
        _bg.gif.bind(_decoder);
        int delay_ms = _bg.gif.next_frame(_decoder);
        _bg.pixels = _bg.gif.pixels();
        _bg_gif_next_us = now + (int64_t)delay_ms * 1000;
        // Background covers every row
        mark_all_dirty();
    }

    // Character GIF (active variant only)
    Sprite* chr = active_char();
    if (chr && chr->is_gif() && now >= _char_gif_next_us) {
        chr->gif.bind(_decoder);
        int delay_ms = chr->gif.next_frame(_decoder);
        chr->pixels = chr->gif.pixels();
        _char_gif_next_us = now + (int64_t)delay_ms * 1000;
        // Character frame changed — mark its bbox dirty
        mark_rows_dirty(chr->base_y, chr->h);
    }

    // Weather GIF (active icon only)
    Sprite* wth = active_weather();
    if (wth && wth->is_gif() && now >= _weather_gif_next_us) {
        wth->gif.bind(_decoder);
        int delay_ms = wth->gif.next_frame(_decoder);
        wth->pixels = wth->gif.pixels();
        _weather_gif_next_us = now + (int64_t)delay_ms * 1000;
        // Weather frame changed — mark its bbox dirty
        mark_rows_dirty(wth->base_y, wth->h);
    }
}

// ============================================================
// Compositing — dirty-aware, dual-core, fused layers
// ============================================================

int FlashModeManager::tick_and_composite(uint16_t* framebuf, int stride) {
    // Advance animations (marks dirty rows when GIF frames change)
    advance_animations();

    // Resolve active sprites
    Sprite* chr = active_char();
    Sprite* wth = active_weather();

    int bob_offset = 0;
    if (chr && _bob_enabled) {
        int64_t now = esp_timer_get_time();
        float elapsed_s = (float)(now - _start_us) / 1000000.0f;
        bob_offset = (int)(sinf(elapsed_s * _bob_speed * 2.0f * M_PI) * _bob_amp);
    }

    // --- Compute current positions and mark dirty from movement ---

    int cur_chr_x = 0, cur_chr_y = 0, cur_chr_w = 0, cur_chr_h = 0;
    if (chr && chr->pixels) {
        cur_chr_x = chr->base_x + bob_offset;
        cur_chr_y = chr->base_y;
        cur_chr_w = chr->w;
        cur_chr_h = chr->h;
    }

    int cur_wth_x = 0, cur_wth_y = 0, cur_wth_w = 0, cur_wth_h = 0;
    if (wth && wth->pixels) {
        cur_wth_x = wth->base_x;
        cur_wth_y = wth->base_y;
        cur_wth_w = wth->w;
        cur_wth_h = wth->h;
    }

    if (!_force_full) {
        // Character moved (bobbing) — mark old and new bbox rows dirty.
        // X movement doesn't change which rows are affected, but the content
        // on those rows changes, so any x-shift marks the full row range.
        if (cur_chr_x != _prev_chr_x || cur_chr_y != _prev_chr_y ||
            cur_chr_w != _prev_chr_w || cur_chr_h != _prev_chr_h) {
            mark_rows_dirty(_prev_chr_y, _prev_chr_h);  // old position
            mark_rows_dirty(cur_chr_y, cur_chr_h);       // new position
        }

        // Weather moved or changed size
        if (cur_wth_x != _prev_wth_x || cur_wth_y != _prev_wth_y ||
            cur_wth_w != _prev_wth_w || cur_wth_h != _prev_wth_h) {
            mark_rows_dirty(_prev_wth_y, _prev_wth_h);
            mark_rows_dirty(cur_wth_y, cur_wth_h);
        }
    }

    _force_full = false;

    // Save positions for next frame
    _prev_chr_x = cur_chr_x; _prev_chr_y = cur_chr_y;
    _prev_chr_w = cur_chr_w; _prev_chr_h = cur_chr_h;
    _prev_wth_x = cur_wth_x; _prev_wth_y = cur_wth_y;
    _prev_wth_w = cur_wth_w; _prev_wth_h = cur_wth_h;

    // --- Build composite params ---

    CompositeParams params = {};
    params.dst        = framebuf;
    params.dst_stride = stride;
    params.dirty_rows = _dirty_rows;

    if (_bg.loaded()) {
        params.bg   = _bg.pixels;
        params.bg_w = _bg.w;
        params.bg_h = _bg.h;
    }

    params.stream        = _stream;
    params.stream_active = _stream_has_content;

    if (wth && wth->pixels) {
        params.wth   = wth->pixels;
        params.wth_w = cur_wth_w;
        params.wth_h = cur_wth_h;
        params.wth_x = cur_wth_x;
        params.wth_y = cur_wth_y;
    }

    if (chr && chr->pixels) {
        params.chr   = chr->pixels;
        params.chr_w = cur_chr_w;
        params.chr_h = cur_chr_h;
        params.chr_x = cur_chr_x;
        params.chr_y = cur_chr_y;
    }

    // --- Dispatch to core(s) --- Toggleable
    constexpr bool USE_DUAL_CORE = true;

    if (USE_DUAL_CORE && s_helper_task) {
        int mid = FRAME_HEIGHT / 2;

        s_helper_params = params;
        s_helper_params.y_start = mid;
        s_helper_params.y_end   = FRAME_HEIGHT;
        xTaskNotifyGive(s_helper_task);

        params.y_start = 0;
        params.y_end   = mid;
        composite_layers_range(params);

        xSemaphoreTake(s_helper_done, portMAX_DELAY);
    } else {
        params.y_start = 0;
        params.y_end   = FRAME_HEIGHT;
        composite_layers_range(params);
    }

    // Count dirty rows for perf monitoring
    int dirty_count = 0;
    for (int i = 0; i < DIRTY_BITMASK_BYTES; i++) {
        // popcount byte
        uint8_t b = _dirty_rows[i];
        b = b - ((b >> 1) & 0x55);
        b = (b & 0x33) + ((b >> 2) & 0x33);
        dirty_count += (b + (b >> 4)) & 0x0F;
    }

    // Clear dirty bitmask for next frame
    memset(_dirty_rows, 0, DIRTY_BITMASK_BYTES);
    return dirty_count;
}
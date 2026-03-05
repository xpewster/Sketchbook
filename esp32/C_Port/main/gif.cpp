#include "gif.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstring>
#include <sys/stat.h>

static const char* TAG = "gif";

// ============================================================
// Draw callback — renders each decoded line to RGB565 output
// ============================================================

static void gif_draw_line(GIFDRAW* pDraw) {
    GifSprite* self = (GifSprite*)pDraw->pUser;
    if (!self || !self->pixels()) return;

    uint16_t* dst = self->pixels() + (pDraw->iY + pDraw->y) * self->width() + pDraw->iX;
    uint8_t*  src = pDraw->pPixels;
    uint16_t* pal = pDraw->pPalette;
    int       w   = pDraw->iWidth;

    if (pDraw->ucHasTransparency) {
        uint8_t trans = pDraw->ucTransparent;
        for (int x = 0; x < w; x++) {
            uint8_t idx = src[x];
            if (idx != trans) dst[x] = pal[idx];
        }
    } else {
        for (int x = 0; x < w; x++) {
            dst[x] = pal[src[x]];
        }
    }
}

// ============================================================
// Construction / Destruction
// ============================================================

GifSprite::GifSprite() {}
GifSprite::~GifSprite() { close(); }

void GifSprite::close() {
    if (_rgb565)    { heap_caps_free(_rgb565);    _rgb565 = nullptr; }
    if (_file_data) { heap_caps_free(_file_data); _file_data = nullptr; }
    _file_size = 0;
    _file_pos = 0;
    _w = _h = 0;
}

// ============================================================
// Load — reads file into PSRAM, parses header, allocs output
// ============================================================

bool GifSprite::load(const char* path) {
    close();

    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return false;
    }

    _file_size = (int)st.st_size;
    _file_data = (uint8_t*)heap_caps_malloc(_file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_file_data) {
        ESP_LOGE(TAG, "Failed to alloc %d bytes for %s", _file_size, path);
        return false;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        close();
        return false;
    }
    size_t read = fread(_file_data, 1, _file_size, f);
    fclose(f);

    if (read != (size_t)_file_size) {
        ESP_LOGE(TAG, "Short read: %s (%d of %d)", path, (int)read, _file_size);
        close();
        return false;
    }

    // Parse GIF header for canvas dimensions (bytes 6-9, little-endian)
    if (_file_size < 10 || _file_data[0] != 'G' || _file_data[1] != 'I' || _file_data[2] != 'F') {
        ESP_LOGE(TAG, "Not a GIF file: %s", path);
        close();
        return false;
    }
    _w = _file_data[6] | (_file_data[7] << 8);
    _h = _file_data[8] | (_file_data[9] << 8);

    // Allocate RGB565 output buffer in PSRAM
    size_t rgb_size = (size_t)_w * _h * sizeof(uint16_t);
    _rgb565 = (uint16_t*)heap_caps_calloc(_w * _h, sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_rgb565) {
        ESP_LOGE(TAG, "Failed to alloc RGB565 (%u KB) for %s",
                 (unsigned)(rgb_size / 1024), path);
        close();
        return false;
    }

    ESP_LOGI(TAG, "Loaded %s: %dx%d, file %d KB, output %u KB",
             path, _w, _h, _file_size / 1024, (unsigned)(rgb_size / 1024));
    return true;
}

// ============================================================
// Bind — open shared decoder on our PSRAM data, restore pos
// ============================================================

bool GifSprite::bind(AnimatedGIF& decoder) {
    if (!_file_data) return false;

    decoder.begin(GIF_PALETTE_RGB565_LE);
    if (!decoder.open(_file_data, _file_size, gif_draw_line)) {
        ESP_LOGE(TAG, "Decoder open failed (error %d)", decoder.getLastError());
        return false;
    }

    // Restore to where we left off last time
    if (_file_pos > 0) {
        decoder.setFilePos(_file_pos);
    }

    return true;
}

// ============================================================
// Next Frame — decode one frame, save position, loop at end
// ============================================================

int GifSprite::next_frame(AnimatedGIF& decoder) {
    int delay = 0;
    int result = decoder.playFrame(false, &delay, this);

    // Save position so next bind() resumes here
    _file_pos = decoder.getFilePos();

    if (result == 0) {
        // Last frame — loop back to start
        decoder.reset();
        _file_pos = 0;
    }

    return (delay > 0) ? delay : 100;
}
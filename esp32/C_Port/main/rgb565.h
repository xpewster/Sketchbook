#pragma once
// r565_loader.h - Load raw RGB565 image files
// Format: 2-byte LE width, 2-byte LE height, then W*H RGB565 pixels (LE)

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "esp_log.h"
#include "esp_heap_caps.h"

struct R565Image {
    uint16_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;

    ~R565Image() { free(); }

    void free() {
        if (pixels) { heap_caps_free(pixels); pixels = nullptr; }
        width = height = 0;
    }

    bool load(const char* path) {
        free();
        FILE* f = fopen(path, "rb");
        if (!f) {
            ESP_LOGE("r565", "Cannot open %s", path);
            return false;
        }

        // Read 4-byte header: width(u16 LE) + height(u16 LE)
        uint8_t hdr[4];
        if (fread(hdr, 1, 4, f) != 4) {
            fclose(f);
            return false;
        }
        width  = hdr[0] | (hdr[1] << 8);
        height = hdr[2] | (hdr[3] << 8);

        size_t npixels = (size_t)width * height;
        pixels = (uint16_t*)heap_caps_malloc(npixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pixels) {
            ESP_LOGE("r565", "Failed to allocate %dx%d (%u KB)", width, height, (unsigned)(npixels * 2 / 1024));
            fclose(f);
            width = height = 0;
            return false;
        }

        size_t read = fread(pixels, 2, npixels, f);
        fclose(f);

        if (read != npixels) {
            ESP_LOGW("r565", "Short read: got %u of %u pixels", (unsigned)read, (unsigned)npixels);
        }

        ESP_LOGI("r565", "Loaded %s: %dx%d", path, width, height);
        return true;
    }
};

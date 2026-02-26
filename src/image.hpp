#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <chrono>

namespace qualia {

// RGB565 pixel type
using Pixel = uint16_t;

constexpr int DISPLAY_WIDTH = 240;
constexpr int DISPLAY_HEIGHT = 960;

// image buffer
class Image {
public:
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels;
    
    Image() = default;
    Image(int w, int h) : width(w), height(h), pixels(w * h, 0) {}
    
    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.resize(w * h);
    }
    
    void clear(Pixel color = 0) {
        std::fill(pixels.begin(), pixels.end(), color);
    }
    
    Pixel& at(int x, int y) { return pixels[y * width + x]; }
    Pixel at(int x, int y) const { return pixels[y * width + x]; }
    
    void setPixel(int x, int y, Pixel color) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            pixels[y * width + x] = color;
        }
    }
    
    Pixel getPixel(int x, int y) const {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            return pixels[y * width + x];
        }
        return 0;
    }
    
    void fillRect(int x, int y, int w, int h, Pixel color) {
        for (int py = y; py < y + h && py < height; ++py) {
            for (int px = x; px < x + w && px < width; ++px) {
                if (px >= 0 && py >= 0) {
                    pixels[py * width + px] = color;
                }
            }
        }
    }
    
    void drawRect(int x, int y, int w, int h, Pixel color) {
        for (int px = x; px < x + w; ++px) {
            setPixel(px, y, color);
            setPixel(px, y + h - 1, color);
        }
        for (int py = y; py < y + h; ++py) {
            setPixel(x, py, color);
            setPixel(x + w - 1, py, color);
        }
    }
    
    // Get raw bytes for transmission (already in correct format)
    const uint8_t* data() const {
        return reinterpret_cast<const uint8_t*>(pixels.data());
    }
    
    size_t dataSize() const {
        return pixels.size() * sizeof(Pixel);
    }
};



} // namespace qualia
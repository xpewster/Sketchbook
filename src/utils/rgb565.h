#pragma once

#include <SFML/Graphics.hpp>
#include "../image.hpp"

using Pixel = uint16_t;

enum class ColorMode: uint8_t {
    RGB565,
    RGB444,
    RGB343,
    RGB332,
};

constexpr int bitsPerPixel(ColorMode mode) {
    switch (mode) {
        case ColorMode::RGB565: return 16;
        case ColorMode::RGB444: return 12;
        case ColorMode::RGB343: return 10;
        case ColorMode::RGB332: return 8;
    }
}

// Count is also N-bit, so max run is limited by color depth
constexpr uint16_t maxRunLength(ColorMode mode) {
    return (1 << bitsPerPixel(mode)) - 1; // 65535, 4095, 1023, 255
}

constexpr uint16_t minRleRunLength() { return 4; } // same for all modes

// Packed byte size for N pixels
inline size_t packedByteSize(size_t nPixels, ColorMode mode) {
    return (nPixels * bitsPerPixel(mode) + 7) / 8;
}

// Convert RGB to RGB565
constexpr Pixel rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<Pixel>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

constexpr Pixel rgb444(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<Pixel>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

constexpr Pixel rgb343(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<Pixel>(((r >> 5) << 7) | ((g >> 5) << 4) | (b >> 5));
}

constexpr Pixel rgb332(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<Pixel>(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

// Convert RenderTexture to RGB for Qualia
void textureToRGB(sf::RenderTexture& texture, qualia::Image& image, ColorMode mode = ColorMode::RGB565) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(x, y));
            switch (mode) {
                case ColorMode::RGB565:
                    image.at(x, y) = rgb565(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB444:
                    image.at(x, y) = rgb444(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB343:
                    image.at(x, y) = rgb343(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB332:
                    image.at(x, y) = rgb332(c.r, c.g, c.b);
                    break;
            }
        }
    }
}

// Rotate 90 degrees clockwise during conversion
void textureToRGBRot90(sf::RenderTexture& texture, qualia::Image& image, ColorMode mode = ColorMode::RGB565) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    // texture is (height, width), output is (width, height)
    // Output pixel (x, y) comes from input pixel (y, width-1-x)
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(y, image.width - 1 - x));
            switch (mode) {
                case ColorMode::RGB565:
                    image.at(x, y) = rgb565(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB444:
                    image.at(x, y) = rgb444(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB343:
                    image.at(x, y) = rgb343(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB332:
                    image.at(x, y) = rgb332(c.r, c.g, c.b);
                    break;
            }
        }
    }
}

// Rotate 90 degrees counter-clockwise during conversion  
void textureToRGBRotNeg90(sf::RenderTexture& texture, qualia::Image& image, ColorMode mode = ColorMode::RGB565) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    // Output pixel (x, y) comes from input pixel (height-1-y, x)
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(image.height - 1 - y, x));
            switch (mode) {
                case ColorMode::RGB565:
                    image.at(x, y) = rgb565(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB444:
                    image.at(x, y) = rgb444(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB343:
                    image.at(x, y) = rgb343(c.r, c.g, c.b);
                    break;
                case ColorMode::RGB332:
                    image.at(x, y) = rgb332(c.r, c.g, c.b);
                    break;
            }
        }
    }
}

void textureToRGB(sf::RenderTexture& texture, qualia::Image& image, ColorMode mode = ColorMode::RGB565, bool negative90 = false) {
    if (negative90) {
        textureToRGBRotNeg90(texture, image, mode);
    } else {
        textureToRGBRot90(texture, image, mode);
    }
}

#pragma once

#include <SFML/Graphics.hpp>
#include <turbojpeg.h>
#include <vector>
#include <stdexcept>

class JpegifyEffect {
private:
    tjhandle compressor = nullptr;
    tjhandle decompressor = nullptr;
    std::vector<unsigned char> rgbaBuffer;
    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;
    
    int quality = 30;
    bool enabled = false;
    
    // Cached result
    sf::Texture cachedTexture;
    bool hasCachedResult = false;
    size_t lastContentHash = 0;
    
    // Samples pixels in a grid pattern across the entire image
    size_t computeGridHash(const unsigned char* pixels, int width, int height) {
        size_t hash = 0;
        int stride = width * 4;
        
        // Sample every gridStep pixels in both dimensions
        constexpr int gridStep = 16;
        
        for (int y = 0; y < height; y += gridStep) {
            for (int x = 0; x < width; x += gridStep) {
                int idx = y * stride + x * 4;
                
                // Combine RGB into hash (skip alpha since JPEG doesn't preserve it)
                uint32_t pixel = pixels[idx] | (pixels[idx + 1] << 8) | (pixels[idx + 2] << 16);
                hash ^= pixel + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
        }
        
        return hash;
    }

public:
    JpegifyEffect() {
        compressor = tjInitCompress();
        decompressor = tjInitDecompress();
        if (!compressor || !decompressor) {
            throw std::runtime_error("Failed to initialize TurboJPEG");
        }
    }
    
    ~JpegifyEffect() {
        if (compressor) tjDestroy(compressor);
        if (decompressor) tjDestroy(decompressor);
        if (jpegBuf) tjFree(jpegBuf);
    }
    
    JpegifyEffect(const JpegifyEffect&) = delete;
    JpegifyEffect& operator=(const JpegifyEffect&) = delete;
    
    void setEnabled(bool e) { enabled = e; invalidateCache(); }
    bool isEnabled() const { return enabled; }
    
    void setQuality(int q) { quality = std::clamp(q, 1, 100); invalidateCache(); }
    int getQuality() const { return quality; }
    
    void invalidateCache() { hasCachedResult = false; lastContentHash = 0; }
    
    bool apply(sf::RenderTexture& texture) {
        if (!enabled) return false;
        
        sf::Vector2u size = texture.getSize();
        int width = static_cast<int>(size.x);
        int height = static_cast<int>(size.y);
        
        // Get image data for hash check
        sf::Image img = texture.getTexture().copyToImage();
        const unsigned char* pixels = img.getPixelsPtr();
        
        // Check if content changed
        size_t currentHash = computeGridHash(pixels, width, height);
        
        if (currentHash == lastContentHash && hasCachedResult) {
            // Content unchanged - just draw cached result
            texture.clear();
            sf::Sprite sprite(cachedTexture);
            texture.draw(sprite);
            texture.display();
            return true;
        }
        
        // Content changed - do JPEG round-trip
        lastContentHash = currentHash;
        
        if (jpegBuf) {
            tjFree(jpegBuf);
            jpegBuf = nullptr;
        }
        
        int result = tjCompress2(
            compressor, pixels, width, 0, height,
            TJPF_RGBA, &jpegBuf, &jpegSize,
            TJSAMP_420, quality, TJFLAG_FASTDCT
        );
        
        if (result != 0) return false;
        
        rgbaBuffer.resize(width * height * 4);
        
        result = tjDecompress2(
            decompressor, jpegBuf, jpegSize,
            rgbaBuffer.data(), width, 0, height,
            TJPF_RGBA, TJFLAG_FASTDCT
        );
        
        if (result != 0) return false;
        
        // Restore alpha
        for (size_t i = 3; i < rgbaBuffer.size(); i += 4) {
            rgbaBuffer[i] = 255;
        }
        
        // Cache the result
        if (!cachedTexture.resize(size)) return false;
        cachedTexture.update(rgbaBuffer.data());
        hasCachedResult = true;
        
        // Draw result
        texture.clear();
        sf::Sprite sprite(cachedTexture);
        texture.draw(sprite);
        texture.display();
        
        return true;
    }

    // Static method: Apply jpegify to an sf::Image directly
    // Preserves alpha channel from original image
    static bool applyToImage(sf::Image& image, int quality) {
        if (quality <= 0 || quality > 100) return false;
        
        tjhandle comp = tjInitCompress();
        tjhandle decomp = tjInitDecompress();
        if (!comp || !decomp) {
            if (comp) tjDestroy(comp);
            if (decomp) tjDestroy(decomp);
            return false;
        }
        
        sf::Vector2u size = image.getSize();
        int width = static_cast<int>(size.x);
        int height = static_cast<int>(size.y);
        const unsigned char* pixels = image.getPixelsPtr();
        
        // Store original alpha channel before JPEG destroys it
        std::vector<unsigned char> alphaChannel(width * height);
        for (int i = 0; i < width * height; i++) {
            alphaChannel[i] = pixels[i * 4 + 3];
        }
        
        unsigned char* jpegBuf = nullptr;
        unsigned long jpegSize = 0;
        
        int result = tjCompress2(
            comp, pixels, width, 0, height,
            TJPF_RGBA, &jpegBuf, &jpegSize,
            TJSAMP_420, quality, TJFLAG_FASTDCT
        );
        
        if (result != 0) {
            tjDestroy(comp);
            tjDestroy(decomp);
            if (jpegBuf) tjFree(jpegBuf);
            return false;
        }
        
        std::vector<unsigned char> rgbaBuffer(width * height * 4);
        
        result = tjDecompress2(
            decomp, jpegBuf, jpegSize,
            rgbaBuffer.data(), width, 0, height,
            TJPF_RGBA, TJFLAG_FASTDCT
        );
        
        tjFree(jpegBuf);
        tjDestroy(comp);
        tjDestroy(decomp);
        
        if (result != 0) return false;
        
        // Restore original alpha channel
        for (int i = 0; i < width * height; i++) {
            rgbaBuffer[i * 4 + 3] = alphaChannel[i];
        }
        
        // Update the image in place
        image.resize(size);  // Ensure correct size
        for (unsigned int y = 0; y < size.y; y++) {
            for (unsigned int x = 0; x < size.x; x++) {
                int idx = (y * width + x) * 4;
                image.setPixel(sf::Vector2u(x, y), sf::Color(
                    rgbaBuffer[idx],
                    rgbaBuffer[idx + 1],
                    rgbaBuffer[idx + 2],
                    rgbaBuffer[idx + 3]
                ));
            }
        }
        
        return true;
    }
};

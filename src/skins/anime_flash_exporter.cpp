#pragma once

#include "flash_exporter.hpp"
#include "skins/skin.h"
#include "image.hpp"
#include "log.hpp"
#include "../utils/jpegify.hpp"

#include <SFML/Graphics.hpp>
#include <unordered_map>

#include "gif.h"
#include "stb_image.h"

class AnimeSkin; // Forward declaration

namespace flash {

class AnimeSkinFlashExporter : public FlashExporter {
public:
    AnimeSkinFlashExporter(const std::string& targetDrive) : FlashExporter(targetDrive) {}
    
    // Export all assets based on skin configuration
    // rotation: Must match the rotation used when streaming frames (Rot90 or RotNeg90)
    ExportResult exportSkin(Skin* skin, ExportRotation rotation) override {
        ExportResult result;
        rotation_ = rotation;  // Store for use by export helpers
        
        if (!ensureAssetDirectory(result)) {
            return result;
        }
        
        const auto& flashConfig = skin->getFlashConfig();
        const auto& params = skin->getParameters();
        const std::string& skinDir = skin->getBaseSkinDir();
        
        // Capture post-processing settings from skin
        jpegifyEnabled_ = getParamBool(params, "skin.effects.jpegify.enabled", false);
        jpegifyQuality_ = getParamInt(params, "skin.effects.jpegify.quality", 30);
        jpegifyLoadingGif_ = getParamBool(params, "skin.effects.jpegify.loadinggif", false);
        
        if (jpegifyEnabled_) {
            LOG_INFO << "Jpegify enabled for flash export, quality=" << jpegifyQuality_ << "\n";
        }
        
        // Export each enabled layer
        if (flashConfig.isLayerFlashed(FlashLayer::Background)) {
            if (!exportBackground(skinDir, params, result)) return result;
        }
        
        if (flashConfig.isLayerFlashed(FlashLayer::Character)) {
            if (!exportCharacter(skinDir, params, result)) return result;
        }
        
        if (flashConfig.isLayerFlashed(FlashLayer::WeatherIcon)) {
            if (!exportWeatherIcons(skinDir, params, result)) return result;
        }
        
        if (flashConfig.isLayerFlashed(FlashLayer::Text)) {
            if (!exportFonts(skin, skinDir, result)) return result;
        }
        
        if (getParamBool(params, "skin.flash.loading", false)) {
            if (!exportLoadingGif(skinDir, result)) return result;
        }
        
        // Generate config file
        if (!generateConfig(skin, result)) return result;
        
        result.success = true;
        LOG_INFO << "Flash export complete: " << result.exportedFiles.size() 
                  << " files, " << result.totalBytes << " bytes\n";
        
        return result;
    }

private:
    // Get parameter helper
    std::string getParam(const std::unordered_map<std::string, std::string>& params, 
                         const std::string& key, const std::string& defaultVal = "") {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : defaultVal;
    }

    std::string getParamFilename(const std::unordered_map<std::string, std::string>& params,
                                 const std::string& key, const std::string& skinDir, const std::string& defaultVal = "") {
        auto it = params.find(key);
        std::string filename;
        if (it != params.end() && !it->second.empty()) {
            filename = it->second;
        }
        // Check if filename is a valid file in skin directory
        if (!filename.empty() && std::filesystem::exists(skinDir + "/" + filename)) {
            return filename;
        }
        return defaultVal;
    }
    
    float getParamFloat(const std::unordered_map<std::string, std::string>& params,
                        const std::string& key, float defaultVal = 0.0f) {
        auto it = params.find(key);
        if (it != params.end()) {
            try { return std::stof(it->second); } catch (...) {}
        }
        return defaultVal;
    }
    
    int getParamInt(const std::unordered_map<std::string, std::string>& params,
                    const std::string& key, int defaultVal = 0) {
        auto it = params.find(key);
        if (it != params.end()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return defaultVal;
    }
    
    bool getParamBool(const std::unordered_map<std::string, std::string>& params,
                      const std::string& key, bool defaultVal = false) {
        auto it = params.find(key);
        if (it != params.end()) {
            return it->second == "true" || it->second == "1" || it->second == "True";
        }
        return defaultVal;
    }
    
    // Member to store current rotation setting
    ExportRotation rotation_ = ExportRotation::Rot90;
    
    // Post-processing settings
    bool jpegifyEnabled_ = false;
    int jpegifyQuality_ = 30;
    bool jpegifyLoadingGif_ = false;
    
    // Apply post-processing effects to an image (jpegify, etc.)
    void applyPostProcessing(sf::Image& img, bool overrideJpegify = false) {
        if (jpegifyEnabled_ && !overrideJpegify) {
            JpegifyEffect::applyToImage(img, jpegifyQuality_);
        }
    }
    
    // Transform sprite position from original coordinates to rotated coordinates
    // For a sprite at (x, y) with size (w, h) in original 240x960 skin:
    // After 90° CW rotation to 960x240, the sprite's new top-left position changes
    // because the image is also rotated (becoming size h x w)
    // 
    // origW, origH are the original skin dimensions (240, 960)
    // spriteW, spriteH are the original sprite dimensions
    std::pair<float, float> transformSpritePosition(float x, float y, float spriteW, float spriteH, int origW, int origH) {
        if (rotation_ == ExportRotation::Rot90) {
            // 90 degrees clockwise
            // Original sprite bottom-left (x, y + h) becomes new top-left
            // New position: (origH - y - spriteH, x)
            return { static_cast<float>(origH) - y - spriteH, x };
        } else {
            // -90 degrees (270 clockwise)
            // Original sprite top-right (x + w, y) becomes new top-left
            // New position: (y, origW - x - spriteW)
            return { y, static_cast<float>(origW) - x - spriteW };
        }
    }
    
    // Get image dimensions from a PNG file
    std::pair<int, int> getImageDimensions(const std::string& path) {
        sf::Image img;
        if (img.loadFromFile(path)) {
            sf::Vector2u size = img.getSize();
            return { static_cast<int>(size.x), static_cast<int>(size.y) };
        }
        return { 0, 0 };
    }
    
    // Rotate an image according to rotation_ setting
    // Returns a new rotated image
    sf::Image rotateImage(const sf::Image& src) {
        sf::Vector2u srcSize = src.getSize();
        unsigned int srcW = srcSize.x;
        unsigned int srcH = srcSize.y;
        
        // After rotation, dimensions are swapped
        sf::Image dst;
        dst.resize(sf::Vector2u(srcH, srcW));
        
        for (unsigned int srcY = 0; srcY < srcH; srcY++) {
            for (unsigned int srcX = 0; srcX < srcW; srcX++) {
                sf::Color pixel = src.getPixel(sf::Vector2u(srcX, srcY));
                unsigned int dstX, dstY;
                
                if (rotation_ == ExportRotation::Rot90) {
                    // 90 degrees clockwise: (x,y) -> (h-1-y, x) in new coords
                    // new coords: width=oldH, height=oldW
                    dstX = srcH - 1 - srcY;
                    dstY = srcX;
                } else {
                    // -90 degrees (270 clockwise): (x,y) -> (y, w-1-x) in new coords
                    dstX = srcY;
                    dstY = srcW - 1 - srcX;
                }
                
                dst.setPixel(sf::Vector2u(dstX, dstY), pixel);
            }
        }
        
        return dst;
    }
    
    // Export background (animated GIF or static RGB565)
    bool exportBackground(const std::string& skinDir,
                          const std::unordered_map<std::string, std::string>& params,
                          ExportResult& result) {
        bool animated = getParamBool(params, "skin.background.animation.enabled", false);
        int frameCount = getParamInt(params, "skin.background.animation.framecount", 1);
        float fps = getParamFloat(params, "skin.background.animation.speed", 1.0f);
        std::string bgFile = getParamFilename(params, "skin.background.png", skinDir);
        
        if (bgFile.empty()) return true;  // No background configured
        
        std::string basePath = skinDir + "/" + bgFile;
        
        if (animated && frameCount > 1) {
            // Export as animated GIF
            std::string outPath = assetDir_ + "background.gif";
            if (!exportAnimationToGif(basePath, frameCount, fps, outPath, result)) {
                return false;
            }
        } else {
            // Export as static RGB565
            std::string outPath = assetDir_ + "background.r565";
            if (!exportImageToRGB565(basePath, outPath, result)) {
                return false;
            }
        }
        
        return true;
    }
    
    // Export character (all temperature states)
    bool exportCharacter(const std::string& skinDir,
                         const std::unordered_map<std::string, std::string>& params,
                         ExportResult& result) {
        // Normal state
        std::string charFile = getParamFilename(params, "skin.character.png", skinDir);
        if (!charFile.empty()) {
            bool animated = getParamBool(params, "skin.character.animation.enabled", false);
            int frameCount = getParamInt(params, "skin.character.animation.framecount", 1);
            float fps = getParamFloat(params, "skin.character.animation.speed", 1.0f);
            std::string basePath = skinDir + "/" + charFile;
            
            if (animated && frameCount > 1) {
                if (!exportAnimationToGif(basePath, frameCount, fps, assetDir_ + "character.gif", result)) {
                    return false;
                }
            } else {
                if (!exportImageToRGB565(basePath, assetDir_ + "character.r565", result)) {
                    return false;
                }
            }
        }
        
        // Warm state
        std::string warmFile = getParamFilename(params, "skin.character.warm.png", skinDir);
        if (!warmFile.empty()) {
            bool animated = getParamBool(params, "skin.character.warm.animation.enabled", 
                                         getParamBool(params, "skin.character.animation.enabled", false));
            int frameCount = getParamInt(params, "skin.character.warm.animation.framecount",
                                         getParamInt(params, "skin.character.animation.framecount", 1));
            float fps = getParamFloat(params, "skin.character.warm.animation.speed",
                                      getParamFloat(params, "skin.character.animation.speed", 1.0f));
            std::string basePath = skinDir + "/" + warmFile;
            
            if (animated && frameCount > 1) {
                if (!exportAnimationToGif(basePath, frameCount, fps, assetDir_ + "character_warm.gif", result)) {
                    return false;
                }
            } else {
                if (!exportImageToRGB565(basePath, assetDir_ + "character_warm.r565", result)) {
                    return false;
                }
            }
        }
        
        // Hot state
        std::string hotFile = getParamFilename(params, "skin.character.hot.png", skinDir);
        if (!hotFile.empty()) {
            bool animated = getParamBool(params, "skin.character.hot.animation.enabled",
                                         getParamBool(params, "skin.character.animation.enabled", false));
            int frameCount = getParamInt(params, "skin.character.hot.animation.framecount",
                                         getParamInt(params, "skin.character.animation.framecount", 1));
            float fps = getParamFloat(params, "skin.character.hot.animation.speed",
                                      getParamFloat(params, "skin.character.animation.speed", 1.0f));
            std::string basePath = skinDir + "/" + hotFile;
            
            if (animated && frameCount > 1) {
                if (!exportAnimationToGif(basePath, frameCount, fps, assetDir_ + "character_hot.gif", result)) {
                    return false;
                }
            } else {
                if (!exportImageToRGB565(basePath, assetDir_ + "character_hot.r565", result)) {
                    return false;
                }
            }
        }
        
        return true;
    }
    
    // Export weather icons
    bool exportWeatherIcons(const std::string& skinDir,
                            const std::unordered_map<std::string, std::string>& params,
                            ExportResult& result) {
        const char* iconKeys[] = {
            "skin.weather.icon.sunny",
            "skin.weather.icon.cloudy", 
            "skin.weather.icon.rainy",
            "skin.weather.icon.thunderstorm",
            "skin.weather.icon.foggy",
            "skin.weather.icon.windy",
            "skin.weather.icon.night"
        };
        const char* outBasenames[] = {
            "weather_sunny",
            "weather_cloudy",
            "weather_rainy",
            "weather_thunderstorm",
            "weather_foggy",
            "weather_windy",
            "weather_night"
        };
        
        for (int i = 0; i < WEATHER_COUNT; i++) {
            std::string iconFile = getParamFilename(params, std::string(iconKeys[i]) + ".png", skinDir);
            if (iconFile.empty()) continue;
            
            std::string basePath = skinDir + "/" + iconFile;
            bool animated = getParamBool(params, std::string(iconKeys[i]) + ".animation.enabled", false);
            int frameCount = getParamInt(params, std::string(iconKeys[i]) + ".animation.framecount", 1);
            float fps = getParamFloat(params, std::string(iconKeys[i]) + ".animation.speed", 1.0f);
            
            if (animated && frameCount > 1) {
                std::string outPath = assetDir_ + outBasenames[i] + ".gif";
                if (!exportAnimationToGif(basePath, frameCount, fps, outPath, result)) {
                    LOG_WARN << "Warning: could not export animated " << iconKeys[i] << "\n";
                }
            } else {
                std::string outPath = assetDir_ + outBasenames[i] + ".r565";
                if (!exportImageToRGB565(basePath, outPath, result)) {
                    LOG_WARN << "Warning: could not export " << iconKeys[i] << "\n";
                }
            }
        }
        
        return true;
    }
    
    // Export fonts (copy PCF files)
    bool exportFonts(Skin* skin, const std::string& skinDir, ExportResult& result) {
        const auto& fontConfigs = skin->getFontConfigs();
        
        for (const auto& fc : fontConfigs) {
            std::string pcfPath = skinDir + "/" + fc.pcfFile;
            
            if (std::filesystem::exists(pcfPath)) {
                std::string outPath = assetDir_ + fc.pcfFile;
                try {
                    std::filesystem::copy_file(pcfPath, outPath, 
                                               std::filesystem::copy_options::overwrite_existing);
                    result.exportedFiles.push_back(fc.pcfFile);
                    result.totalBytes += std::filesystem::file_size(outPath);
                    LOG_INFO << "Copied font: " << fc.pcfFile << "\n";
                } catch (const std::exception& e) {
                    LOG_WARN << "Warning: Could not copy font " << fc.pcfFile << ": " << e.what() << "\n";
                }
            } else {
                LOG_WARN << "Warning: PCF font not found: " << pcfPath << "\n";
                LOG_WARN << "  (You may need to convert " << fc.ttfFile << " to PCF format)\n";
            }
        }
        
        return true;
    }
    
    // Export loading.gif if present in skin directory
    bool exportLoadingGif(const std::string& skinDir, ExportResult& result) {
        std::string loadingPath = skinDir + "/loading.gif";
        
        if (!std::filesystem::exists(loadingPath)) {
            LOG_WARN << "Warning: loading.gif not found in skin directory: " << loadingPath << "\n";
            return false;
        }
        
        std::string outPath = assetDir_ + "loading.gif";
        
        // If post-processing is enabled, process the GIF frame by frame
        if (jpegifyEnabled_ && jpegifyLoadingGif_) {
            return processAndExportGif(loadingPath, outPath, result, !jpegifyLoadingGif_);
        }
        
        // Otherwise just copy directly
        try {
            std::filesystem::copy_file(loadingPath, outPath,
                                    std::filesystem::copy_options::overwrite_existing);
            result.exportedFiles.push_back("loading.gif");
            result.totalBytes += std::filesystem::file_size(outPath);
            LOG_INFO << "Copied loading.gif\n";
            return true;
        } catch (const std::exception& e) {
            LOG_WARN << "Warning: Could not copy loading.gif: " << e.what() << "\n";
            return false;
        }
    }
    
    // Export PNG animation frames to GIF
    bool exportAnimationToGif(const std::string& basePath, int frameCount, float fps,
                              const std::string& outPath, ExportResult& result) {
        std::string pathNoExt = basePath.substr(0, basePath.rfind(".png"));
        
        // Load first frame to get dimensions (after rotation)
        std::string firstFramePath = pathNoExt + ".0.png";
        sf::Image srcFirstFrame;
        if (!srcFirstFrame.loadFromFile(firstFramePath)) {
            result.error = "Failed to load first frame: " + firstFramePath;
            return false;
        }
        
        // Rotate first frame to get final dimensions
        sf::Image firstFrame = rotateImage(srcFirstFrame);
        
        // Apply post-processing to get accurate dimensions
        applyPostProcessing(firstFrame);
        
        sf::Vector2u size = firstFrame.getSize();
        int width = size.x;
        int height = size.y;
        int delay = static_cast<int>(100.0f / fps);  // GIF delay in centiseconds
        
        // Initialize GIF writer with rotated dimensions
        GifWriter writer;
        if (!GifBegin(&writer, outPath.c_str(), width, height, delay)) {
            result.error = "Failed to create GIF: " + outPath;
            return false;
        }
        
        // Write each frame (rotated)
        std::vector<uint8_t> frameData(width * height * 4);
        
        for (int i = 0; i < frameCount; i++) {
            std::string framePath = pathNoExt + "." + std::to_string(i) + ".png";
            sf::Image srcFrame;
            if (!srcFrame.loadFromFile(framePath)) {
                LOG_WARN << "Warning: Missing frame " << framePath << "\n";
                continue;
            }
            
            // Rotate frame to match display orientation
            sf::Image frame = rotateImage(srcFrame);
            
            // Apply post-processing effects
            applyPostProcessing(frame);
            
            // Convert to RGBA format for gif.h
            const uint8_t* pixels = frame.getPixelsPtr();
            memcpy(frameData.data(), pixels, width * height * 4);
            
            if (!GifWriteFrame(&writer, frameData.data(), width, height, delay)) {
                result.error = "Failed to write GIF frame " + std::to_string(i);
                GifEnd(&writer);
                return false;
            }
        }
        
        if (!GifEnd(&writer)) {
            result.error = "Failed to finalize GIF";
            return false;
        }
        
        result.exportedFiles.push_back(std::filesystem::path(outPath).filename().string());
        result.totalBytes += std::filesystem::file_size(outPath);
        LOG_INFO << "Created GIF: " << outPath << " (" << frameCount << " frames)\n";
        
        return true;
    }
    
    // Export single image to raw RGB565 format
    bool exportImageToRGB565(const std::string& inPath, const std::string& outPath,
                             ExportResult& result) {
        sf::Image srcImg;
        if (!srcImg.loadFromFile(inPath)) {
            result.error = "Failed to load image: " + inPath;
            return false;
        }
        
        // Apply rotation to match display orientation
        sf::Image img = rotateImage(srcImg);
        
        // Apply post-processing effects
        applyPostProcessing(img);
        
        sf::Vector2u size = img.getSize();
        uint16_t width = static_cast<uint16_t>(size.x);
        uint16_t height = static_cast<uint16_t>(size.y);
        
        std::ofstream out(outPath, std::ios::binary);
        if (!out) {
            result.error = "Failed to create output file: " + outPath;
            return false;
        }
        
        // Write header: width (2 bytes), height (2 bytes), little-endian
        out.write(reinterpret_cast<const char*>(&width), 2);
        out.write(reinterpret_cast<const char*>(&height), 2);
        
        // Write pixels row by row
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < width; x++) {
                sf::Color c = img.getPixel(sf::Vector2u(x, y));
                uint16_t rgb565;
                
                // Check for transparency (if alpha < 128, treat as transparent)
                if (c.a < 128) {
                    rgb565 = TRANSPARENT_RGB565;
                } else {
                    rgb565 = toRGB565(c.r, c.g, c.b);
                    // Avoid accidental transparent color
                    if (rgb565 == TRANSPARENT_RGB565) {
                        rgb565 = 0xF81E;  // Slightly different magenta
                    }
                }
                
                out.write(reinterpret_cast<const char*>(&rgb565), 2);
            }
        }
        
        out.close();
        
        result.exportedFiles.push_back(std::filesystem::path(outPath).filename().string());
        result.totalBytes += std::filesystem::file_size(outPath);
        LOG_INFO << "Created RGB565: " << outPath << " (" << width << "x" << height << ")\n";
        
        return true;
    }

    // Process a GIF file: load frames, apply post-processing, write new GIF
    bool processAndExportGif(const std::string& inPath, const std::string& outPath,
                            ExportResult& result, bool overrideJpegify = false) {
        // Read entire file into memory
        std::ifstream file(inPath, std::ios::binary | std::ios::ate);
        if (!file) {
            LOG_WARN << "Failed to open GIF: " << inPath << "\n";
            return false;
        }
        
        size_t fileSize = file.tellg();
        file.seekg(0);
        std::vector<unsigned char> fileData(fileSize);
        file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        file.close();
        
        // Load GIF frames using stb_image
        int* delays = nullptr;
        int width, height, frameCount, comp;
        
        unsigned char* pixels = stbi_load_gif_from_memory(
            fileData.data(), static_cast<int>(fileSize),
            &delays, &width, &height, &frameCount, &comp, 4  // Request RGBA
        );
        
        if (!pixels) {
            LOG_WARN << "Failed to decode GIF: " << inPath << "\n";
            return false;
        }
        
        // Calculate average delay for GIF writer (gif.h uses centiseconds)
        int avgDelay = 10;  // Default 100ms
        if (delays && frameCount > 0) {
            int totalDelay = 0;
            for (int i = 0; i < frameCount; i++) {
                totalDelay += delays[i];
            }
            avgDelay = (totalDelay / frameCount) / 10;  // Convert ms to centiseconds
            if (avgDelay < 1) avgDelay = 1;
        }
        
        // Initialize GIF writer
        GifWriter writer;
        if (!GifBegin(&writer, outPath.c_str(), width, height, avgDelay)) {
            stbi_image_free(pixels);
            if (delays) stbi_image_free(delays);
            LOG_WARN << "Failed to create output GIF: " << outPath << "\n";
            return false;
        }
        
        // Process each frame
        size_t frameSize = width * height * 4;
        std::vector<uint8_t> frameBuffer(frameSize);
        
        for (int i = 0; i < frameCount; i++) {
            // Create sf::Image from frame data
            sf::Image frame(sf::Vector2u(width, height));
            const unsigned char* framePixels = pixels + (i * frameSize);
            
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int idx = (y * width + x) * 4;
                    frame.setPixel(sf::Vector2u(x, y), sf::Color(
                        framePixels[idx],
                        framePixels[idx + 1],
                        framePixels[idx + 2],
                        framePixels[idx + 3]
                    ));
                }
            }
            
            // Apply post-processing (jpegify, etc.)
            applyPostProcessing(frame, overrideJpegify);
            
            // Get processed pixels and write frame
            const uint8_t* processedPixels = frame.getPixelsPtr();
            memcpy(frameBuffer.data(), processedPixels, frameSize);
            
            // Use per-frame delay if available, otherwise average
            int frameDelay = (delays && delays[i] > 0) ? (delays[i] / 10) : avgDelay;
            if (frameDelay < 1) frameDelay = 1;
            
            if (!GifWriteFrame(&writer, frameBuffer.data(), width, height, frameDelay)) {
                GifEnd(&writer);
                stbi_image_free(pixels);
                if (delays) stbi_image_free(delays);
                LOG_WARN << "Failed to write GIF frame " << i << "\n";
                return false;
            }
        }
        
        GifEnd(&writer);
        stbi_image_free(pixels);
        if (delays) stbi_image_free(delays);
        
        result.exportedFiles.push_back(std::filesystem::path(outPath).filename().string());
        result.totalBytes += std::filesystem::file_size(outPath);
        LOG_INFO << "Processed GIF: " << outPath << " (" << frameCount << " frames)\n";
        
        return true;
    }
    
    // Generate config.txt for remote
    bool generateConfig(Skin* skin, ExportResult& result) {
        const auto& flashConfig = skin->getFlashConfig();
        const auto& params = skin->getParameters();
        
        std::string configPath = assetDir_ + "config.txt";
        std::ofstream cfg(configPath);
        if (!cfg) {
            result.error = "Failed to create config file: " + configPath;
            return false;
        }
        
        // Original skin dimensions (before rotation)
        const int origW = 960;
        const int origH = 240;
        // After rotation, dimensions are swapped
        const int dispW = origH;
        const int dispH = origW;
        
        // Header
        cfg << "# Flash mode configuration\n";
        cfg << "# Auto-generated by Sketchbook\n\n";

        // Skin name
        cfg << "skin_name=" << skin->name << "\n\n";
        
        // Display dimensions (after rotation)
        cfg << "display_w=" << dispW << "\n";
        cfg << "display_h=" << dispH << "\n\n";
        
        // Layer enables
        cfg << "# Layer enables\n";
        cfg << "bg_enabled=" << (flashConfig.isLayerFlashed(FlashLayer::Background) ? "1" : "0") << "\n";
        cfg << "char_enabled=" << (flashConfig.isLayerFlashed(FlashLayer::Character) ? "1" : "0") << "\n";
        cfg << "weather_enabled=" << (flashConfig.isLayerFlashed(FlashLayer::WeatherIcon) ? "1" : "0") << "\n";
        cfg << "text_enabled=" << (flashConfig.isLayerFlashed(FlashLayer::Text) ? "1" : "0") << "\n\n";
        
        // Background config
        if (flashConfig.isLayerFlashed(FlashLayer::Background)) {
            bool animated = getParamBool(params, "skin.background.animation.enabled", false);
            int frameCount = getParamInt(params, "skin.background.animation.framecount", 1);
            
            cfg << "# Background\n";
            cfg << "bg_animated=" << (animated && frameCount > 1 ? "1" : "0") << "\n";
            cfg << "bg_file=" << (animated && frameCount > 1 ? "background.gif" : "background.r565") << "\n";
            cfg << "bg_fps=" << getParamFloat(params, "skin.background.animation.speed", 1.0f) << "\n\n";
        }
        
        // Character config
        if (flashConfig.isLayerFlashed(FlashLayer::Character)) {
            bool animated = getParamBool(params, "skin.character.animation.enabled", false);
            int frameCount = getParamInt(params, "skin.character.animation.framecount", 1);
            bool hasWarm = !getParamFilename(params, "skin.character.warm.png", skin->getBaseSkinDir()).empty();
            bool hasHot = !getParamFilename(params, "skin.character.hot.png", skin->getBaseSkinDir()).empty();
            
            // Get character image dimensions for proper position transform
            std::string charFile = getParam(params, "skin.character.png");
            std::string charPath = skin->getBaseSkinDir() + "/" + charFile;
            auto [charW, charH] = getImageDimensions(charPath);
            
            // Get original position and transform with sprite size
            float origX = getParamFloat(params, "skin.character.x", 0);
            float origY = getParamFloat(params, "skin.character.y", 0);
            auto [newX, newY] = transformSpritePosition(origX, origY, (float)charW, (float)charH, origW, origH);
            
            cfg << "# Character\n";
            cfg << "char_animated=" << (animated && frameCount > 1 ? "1" : "0") << "\n";
            cfg << "char_file=" << (animated && frameCount > 1 ? "character.gif" : "character.r565") << "\n";
            cfg << "char_fps=" << getParamFloat(params, "skin.character.animation.speed", 1.0f) << "\n";
            cfg << "char_x=" << newX << "\n";
            cfg << "char_y=" << newY << "\n";
            cfg << "char_flip=" << (getParamBool(params, "skin.character.flip", false) ? "1" : "0") << "\n";
            cfg << "char_bob=" << (getParamBool(params, "skin.character.bobbing.enabled", false) ? "1" : "0") << "\n";
            cfg << "char_bob_speed=" << getParamFloat(params, "skin.character.bobbing.speed", 1.0f) << "\n";
            cfg << "char_bob_amp=" << getParamFloat(params, "skin.character.bobbing.amplitude", 5.0f) << "\n";
            cfg << "char_has_warm=" << (hasWarm ? "1" : "0") << "\n";
            cfg << "char_has_hot=" << (hasHot ? "1" : "0") << "\n";
            
            if (hasWarm) {
                bool warmAnimated = getParamBool(params, "skin.character.warm.animation.enabled", animated);
                int warmFrameCount = getParamInt(params, "skin.character.warm.animation.framecount", frameCount);
                cfg << "char_warm_file=" << (warmAnimated && warmFrameCount > 1 ? "character_warm.gif" : "character_warm.r565") << "\n";
            }
            if (hasHot) {
                bool hotAnimated = getParamBool(params, "skin.character.hot.animation.enabled", animated);
                int hotFrameCount = getParamInt(params, "skin.character.hot.animation.framecount", frameCount);
                cfg << "char_hot_file=" << (hotAnimated && hotFrameCount > 1 ? "character_hot.gif" : "character_hot.r565") << "\n";
            }
            cfg << "\n";
        }
        
        // Temperature thresholds
        cfg << "# Temperature thresholds (Celsius)\n";
        cfg << "temp_warm=" << skin->getWarmThreshold() << "\n";
        cfg << "temp_hot=" << skin->getHotThreshold() << "\n\n";
        cfg << "thresholds_using_percentage=" << (skin->getThresholdsUsingPercentage() ? "1" : "0") << "\n\n";
        
        // Weather icon config
        if (flashConfig.isLayerFlashed(FlashLayer::WeatherIcon)) {
            float origWX = getParamFloat(params, "skin.weather.icon.x", 0);
            float origWY = getParamFloat(params, "skin.weather.icon.y", 0);
            float origWW = getParamFloat(params, "skin.weather.icon.width", 32);
            float origWH = getParamFloat(params, "skin.weather.icon.height", 32);
            // Transform position using sprite dimensions
            auto [newWX, newWY] = transformSpritePosition(origWX, origWY, origWW, origWH, origW, origH);
            // After rotation, width and height swap
            
            cfg << "# Weather icons\n";
            cfg << "weather_x=" << newWX << "\n";
            cfg << "weather_y=" << newWY << "\n";
            cfg << "weather_w=" << origWH << "\n";  // Swapped
            cfg << "weather_h=" << origWW << "\n";  // Swapped
            
            // Animation info for each weather type
            const char* weatherTypes[] = {"sunny", "cloudy", "rainy", "thunderstorm", "foggy", "windy", "night"};
            for (const char* wtype : weatherTypes) {
                std::string keyBase = std::string("skin.weather.icon.") + wtype;
                bool animated = getParamBool(params, keyBase + ".animation.enabled", false);
                int frameCount = getParamInt(params, keyBase + ".animation.framecount", 1);
                float fps = getParamFloat(params, keyBase + ".animation.speed", 1.0f);
                
                if (animated && frameCount > 1) {
                    cfg << "weather_" << wtype << "_file=weather_" << wtype << ".gif\n";
                    cfg << "weather_" << wtype << "_fps=" << fps << "\n";
                } else {
                    cfg << "weather_" << wtype << "_file=weather_" << wtype << ".r565\n";
                }
            }
            cfg << "\n";
        }
        
        // Text config
        if (flashConfig.isLayerFlashed(FlashLayer::Text)) {
            cfg << "# Text rendering\n";
            
            // Font config
            const auto& fontConfigs = skin->getFontConfigs();
            if (!fontConfigs.empty()) {
                cfg << "font_file=" << fontConfigs[0].pcfFile << "\n";
            }
            
            // Weather text
            if (params.find("skin.weather.text.x") != params.end()) {
                cfg << "weather_text_x=" << getParamFloat(params, "skin.weather.text.x", 0) << "\n";
                cfg << "weather_text_y=" << getParamFloat(params, "skin.weather.text.y", 0) << "\n";
                cfg << "weather_text_color=" << getParam(params, "skin.weather.text.color", "FFFFFF") << "\n";
                cfg << "weather_text_size=" << getParamInt(params, "skin.weather.text.size", 14) << "\n";
            }
            
            // CPU text
            if (params.find("skin.hwmon.cpu.usage.text.x") != params.end()) {
                cfg << "cpu_text_x=" << getParamFloat(params, "skin.hwmon.cpu.usage.text.x", 0) << "\n";
                cfg << "cpu_text_y=" << getParamFloat(params, "skin.hwmon.cpu.usage.text.y", 0) << "\n";
                cfg << "cpu_text_color=" << getParam(params, "skin.hwmon.cpu.usage.text.color", "FFFFFF") << "\n";
                cfg << "cpu_text_size=" << getParamInt(params, "skin.hwmon.cpu.usage.text.size", 14) << "\n";
                cfg << "cpu_combine=" << (getParamBool(params, "skin.hwmon.cpu.combine", false) ? "1" : "0") << "\n";
            }
            
            // Memory text
            if (params.find("skin.hwmon.mem.usage.text.x") != params.end()) {
                cfg << "mem_text_x=" << getParamFloat(params, "skin.hwmon.mem.usage.text.x", 0) << "\n";
                cfg << "mem_text_y=" << getParamFloat(params, "skin.hwmon.mem.usage.text.y", 0) << "\n";
                cfg << "mem_text_color=" << getParam(params, "skin.hwmon.mem.usage.text.color", "FFFFFF") << "\n";
                cfg << "mem_text_size=" << getParamInt(params, "skin.hwmon.mem.usage.text.size", 14) << "\n";
            }
            
            // Train text
            if (params.find("skin.hwmon.train.next.text.x") != params.end()) {
                cfg << "train_text_x=" << getParamFloat(params, "skin.hwmon.train.next.text.x", 0) << "\n";
                cfg << "train_text_y=" << getParamFloat(params, "skin.hwmon.train.next.text.y", 0) << "\n";
                cfg << "train_text_color=" << getParam(params, "skin.hwmon.train.next.text.color", "FFFFFF") << "\n";
                cfg << "train_text_size=" << getParamInt(params, "skin.hwmon.train.next.text.size", 14) << "\n";
            }
            
            cfg << "\n";
        }
        
        // Transparent color key
        cfg << "# Transparent color key (RGB565)\n";
        cfg << "transparent_color=F81F\n";
        
        cfg.close();
        
        result.exportedFiles.push_back("config.txt");
        result.totalBytes += std::filesystem::file_size(configPath);
        LOG_INFO << "Created config: " << configPath << "\n";
        
        return true;
    }
};

} // namespace flash

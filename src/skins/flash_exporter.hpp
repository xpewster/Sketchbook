#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstdint>
#include "../utils/condition.h"

// Forward declarations
class Skin;

namespace flash {

// RGB565 conversion helper
inline uint16_t toRGB565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Transparent color key
constexpr uint16_t TRANSPARENT_RGB565 = 0xF81F;  // Magenta

// Rotation to apply to exported assets (matches display orientation)
enum class ExportRotation {
    Rot90,    // 90 degrees clockwise (default)
    RotNeg90  // 90 degrees counter-clockwise (rotate180)
};

// Weather icon indices (must match remote code)
enum WeatherIconIndex : uint8_t {
    WEATHER_SUNNY = 0,
    WEATHER_CLOUDY = 1,
    WEATHER_RAINY = 2,
    WEATHER_THUNDERSTORM = 3,
    WEATHER_FOGGY = 4,
    WEATHER_WINDY = 5,
    WEATHER_NIGHT = 6,
    WEATHER_COUNT = 7
};

// Character state indices
enum CharacterStateIndex : uint8_t {
    CHAR_NORMAL = 0,
    CHAR_WARM = 1,
    CHAR_HOT = 2,
    CHAR_STATE_COUNT = 3
};

// Result of an export operation
struct ExportResult {
    bool success = false;
    std::string error;
    std::vector<std::string> exportedFiles;
    size_t totalBytes = 0;
};

// Flash mode protocol message
// Simplified protocol - fixed size header followed by dirty rect data
struct FlashStatsMessage {
    static constexpr uint8_t MSG_TYPE = 0x03;
    
    uint8_t msgType = MSG_TYPE;
    uint8_t weatherIconIndex;  // 0-6, or 0xFF for none
    uint8_t flags;             // bit0: cpu_warm, bit1: cpu_hot, bit2: weather_avail, bit3: train0_avail, bit4: train1_avail
    uint16_t cpuPercent10;     // CPU percent * 10
    uint16_t cpuTemp10;        // CPU temp * 10
    uint16_t memPercent10;     // Memory percent * 10
    int16_t weatherTemp10;     // Weather temp * 10 (signed)
    uint16_t train0Mins10;     // Train 0 minutes * 10
    uint16_t train1Mins10;     // Train 1 minutes * 10
    
    // Flag bits
    static constexpr uint8_t FLAG_CPU_WARM = 0x01;
    static constexpr uint8_t FLAG_CPU_HOT = 0x02;
    static constexpr uint8_t FLAG_WEATHER_AVAIL = 0x04;
    static constexpr uint8_t FLAG_TRAIN0_AVAIL = 0x08;
    static constexpr uint8_t FLAG_TRAIN1_AVAIL = 0x10;
    
    // Serialize to bytes (fixed 16-byte header)
    std::vector<uint8_t> serialize(uint8_t rectCount) const {
        std::vector<uint8_t> data;
        data.reserve(17);  // 16 bytes header + 1 byte rect_count
        
        // Fixed fields
        data.push_back(msgType);
        data.push_back(weatherIconIndex);
        data.push_back(flags);
        data.push_back(cpuPercent10 & 0xFF);
        data.push_back((cpuPercent10 >> 8) & 0xFF);
        data.push_back(cpuTemp10 & 0xFF);
        data.push_back((cpuTemp10 >> 8) & 0xFF);
        data.push_back(memPercent10 & 0xFF);
        data.push_back((memPercent10 >> 8) & 0xFF);
        data.push_back(weatherTemp10 & 0xFF);
        data.push_back((weatherTemp10 >> 8) & 0xFF);
        data.push_back(train0Mins10 & 0xFF);
        data.push_back((train0Mins10 >> 8) & 0xFF);
        data.push_back(train1Mins10 & 0xFF);
        data.push_back((train1Mins10 >> 8) & 0xFF);
        
        // Rect count for any streamed layers
        data.push_back(rectCount);
        
        return data;
    }
};

// Get weather icon index for flash mode
int getWeatherIconIndex(const WeatherData& weather) {
    if (!weather.available) return 0xFF;
    
    const std::string& weatherType = getWeatherIconNameSimplified(weather);
    
    if (weatherType == "sunny") return WEATHER_SUNNY;
    if (weatherType == "cloudy") return WEATHER_CLOUDY;
    if (weatherType == "rainy") return WEATHER_RAINY;
    if (weatherType == "thunderstorm") return WEATHER_THUNDERSTORM;
    if (weatherType == "foggy") return WEATHER_FOGGY;
    if (weatherType == "windy") return WEATHER_WINDY;
    
    if (weather.isNight) return WEATHER_NIGHT;
    return WEATHER_SUNNY;
}

// Build flash stats message from current state
FlashStatsMessage buildFlashStats(const SystemStats& stats, const WeatherData& weather,
                                          const TrainData& train, Skin* skin) {
    FlashStatsMessage msg;
    
    msg.weatherIconIndex = getWeatherIconIndex(weather);
    
    msg.flags = 0;
    if (skin->getThresholdsUsingPercentage()) {
        if (stats.cpuPercent >= skin->getWarmThreshold()) {
            msg.flags |= FlashStatsMessage::FLAG_CPU_WARM;
        }
        if (stats.cpuPercent >= skin->getHotThreshold()) {
            msg.flags |= FlashStatsMessage::FLAG_CPU_HOT;
        }
    } else {
        if (stats.cpuTempC >= skin->getWarmThreshold()) {
            msg.flags |= FlashStatsMessage::FLAG_CPU_WARM;
        }
        if (stats.cpuTempC >= skin->getHotThreshold()) {
            msg.flags |= FlashStatsMessage::FLAG_CPU_HOT;
        }
    }
    if (weather.available) {
        msg.flags |= FlashStatsMessage::FLAG_WEATHER_AVAIL;
    }
    if (train.available0) {
        msg.flags |= FlashStatsMessage::FLAG_TRAIN0_AVAIL;
    }
    if (train.available1) {
        msg.flags |= FlashStatsMessage::FLAG_TRAIN1_AVAIL;
    }
    
    msg.cpuPercent10 = static_cast<uint16_t>(stats.cpuPercent * 10);
    msg.cpuTemp10 = static_cast<uint16_t>(stats.cpuTempC * 10);
    msg.memPercent10 = static_cast<uint16_t>(stats.memPercent * 10);
    msg.weatherTemp10 = static_cast<int16_t>(weather.currentTemp * 10);
    msg.train0Mins10 = static_cast<uint16_t>(train.minsToNextTrain0 * 10);
    msg.train1Mins10 = static_cast<uint16_t>(train.minsToNextTrain1 * 10);
    
    return msg;
}

// Abstract base class for flash exporters
// Each skin type can implement its own exporter
class FlashExporter {
public:
    FlashExporter(const std::string& targetDrive) : targetDrive_(targetDrive) {
        // Ensure drive path ends with separator
        if (!targetDrive_.empty() && targetDrive_.back() != '/' && targetDrive_.back() != '\\') {
            targetDrive_ += "/";
        }
        assetDir_ = targetDrive_ + "flash_assets/";
    }
    
    virtual ~FlashExporter() = default;
    
    // Export all assets based on skin configuration
    // rotation: Must match the rotation used when streaming frames
    // Must be implemented by derived classes
    virtual ExportResult exportSkin(Skin* skin, ExportRotation rotation) = 0;
    
    // Check if target drive is flashable (has FLASHABLE marker in root)
    bool isFlashable() const {
        return std::filesystem::exists(targetDrive_ + "FLASHABLE");
    }
    
    // Check if flash assets exist on the device
    bool hasFlashAssets() const {
        if (!std::filesystem::exists(assetDir_)) {
            return false;
        }
        // Check if config.txt exists (required for flash mode)
        return std::filesystem::exists(assetDir_ + "config.txt");
    }

    // Get the name of the skin currently flashed to the board
    std::string getLastFlashedSkinName() const {
        std::string configPath = assetDir_ + "config.txt";
        if (!std::filesystem::exists(configPath)) {
            return "";
        }
        std::ifstream cfg(configPath);
        if (!cfg) {
            return "";
        }
        std::string line;
        while (std::getline(cfg, line)) {
            if (line.rfind("skin_name=", 0) == 0) {
                return line.substr(10);  // Length of "skin_name="
            }
        }
        return "";
    }
    
    // Clear all files from asset directory
    bool clearAssetDirectory() {
        if (!isFlashable()) {
            return false;  // Safety check
        }
        if (!std::filesystem::exists(assetDir_)) {
            return true;  // Nothing to clear
        }
        try {
            for (const auto& entry : std::filesystem::directory_iterator(assetDir_)) {
                std::filesystem::remove_all(entry.path());
            }
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    // Get the asset directory path
    const std::string& getAssetDir() const { return assetDir_; }
    
    // Get the target drive path
    const std::string& getTargetDrive() const { return targetDrive_; }

protected:
    std::string targetDrive_;
    std::string assetDir_;
    
    // Ensure the asset directory exists and is on a flashable drive
    bool ensureAssetDirectory(ExportResult& result) {
        // Verify target drive exists
        if (!std::filesystem::exists(targetDrive_)) {
            result.error = "Target drive not found: " + targetDrive_;
            return false;
        }
        
        // Check for FLASHABLE marker
        if (!isFlashable()) {
            result.error = "Drive is not flashable (missing FLASHABLE marker): " + targetDrive_;
            return false;
        }
        
        // Create asset directory
        try {
            std::filesystem::create_directories(assetDir_);
        } catch (const std::exception& e) {
            result.error = "Failed to create asset directory: " + std::string(e.what());
            return false;
        }
        
        return true;
    }
};

} // namespace flash

#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <unordered_map>
#include <filesystem>

#include "../log.hpp"
#include "../system_stats.h"
#include "../utils/xml.h"
#include "../weather.hpp"
#include "../train.hpp"

// Flash mode layer flags
enum class FlashLayer : uint8_t {
    None        = 0,
    Background  = 1 << 0,
    Character   = 1 << 1,
    WeatherIcon = 1 << 2,
    Text        = 1 << 3,
    All         = 0xFF
};

inline FlashLayer operator|(FlashLayer a, FlashLayer b) {
    return static_cast<FlashLayer>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline FlashLayer operator&(FlashLayer a, FlashLayer b) {
    return static_cast<FlashLayer>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool hasLayer(FlashLayer flags, FlashLayer layer) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(layer)) != 0;
}

// Flash mode configuration
struct FlashConfig {
    FlashLayer enabledLayers = FlashLayer::None;
    bool previewComposite = true;  // true: show full skin in preview, false: show magenta where flashed
    
    bool isLayerFlashed(FlashLayer layer) const {
        return hasLayer(enabledLayers, layer);
    }
};

// Font configuration entry
struct FontConfig {
    int index = 0;
    std::string ttfFile;   // TTF filename
    std::string pcfFile;   // PCF filename (same name, different extension)
    sf::Font font;
    bool loaded = false;
    
    // Font styling
    sf::Color fillColor = sf::Color::White;
    bool outlineEnabled = false;
    float outlineThickness = 0.0f;
    sf::Color outlineColor = sf::Color::Black;
};

// Character temperature state
enum class CharacterTempState {
    Normal,
    Warm,
    Hot
};

class Skin {
private:
    
protected:
    sf::Font defaultFont;
    const int DISPLAY_WIDTH;
    const int DISPLAY_HEIGHT;
    std::unordered_map<std::string, std::string> parameters;
    std::vector<FontConfig> fontConfigs;
    unsigned long frameCount = 0;
    std::string baseSkinDir;
    bool parametersRefreshed = false;
    
    // Flash mode
    FlashConfig flashConfig;
    
    // Temperature thresholds
    float warmTempThreshold = 60.0f;  // Celsius
    float hotTempThreshold = 80.0f;   // Celsius

    // Helper to determine character temperature state
    CharacterTempState getCharacterTempState(float tempC) const {
        if (tempC >= hotTempThreshold) return CharacterTempState::Hot;
        if (tempC >= warmTempThreshold) return CharacterTempState::Warm;
        return CharacterTempState::Normal;
    }
    
    // Helper to parse hex color from parameter
    sf::Color parseHexColor(const std::string& key, sf::Color defaultVal = sf::Color::White) const {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            std::string hexStr = it->second;
            if (!hexStr.empty() && hexStr[0] == '#') {
                hexStr = hexStr.substr(1);
            }
            try {
                unsigned int hex = std::stoul(hexStr, nullptr, 16);
                return sf::Color((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
            } catch (...) {
                LOG_WARN << "Invalid color for key " << key << ": " << it->second << "\n";
                return defaultVal;
            }
        }
        return defaultVal;
    }
    
    // Load fonts from configuration
    void loadFonts() {
        fontConfigs.clear();
        
        for (int i = 0; i < 16; i++) {  // Support up to 16 fonts
            std::string ttfKey = "skin.fonts.font[id=" + std::to_string(i) + "].ttf";
            
            auto it = parameters.find(ttfKey);
            if (it != parameters.end() && !it->second.empty()) {
                FontConfig fc;
                fc.index = i;
                fc.ttfFile = it->second;
                
                // PCF file has same name but .pcf extension
                std::string baseName = fc.ttfFile;
                size_t dotPos = baseName.rfind('.');
                if (dotPos != std::string::npos) {
                    fc.pcfFile = baseName.substr(0, dotPos) + ".pcf";
                } else {
                    fc.pcfFile = baseName + ".pcf";
                }
                
                // Load the TTF font
                std::string fullPath = baseSkinDir + "/" + fc.ttfFile;
                if (fc.font.openFromFile(fullPath)) {
                    fc.loaded = true;
                    if ((flashConfig.enabledLayers & FlashLayer::Background) != FlashLayer::None) {
                        // Disable anti-aliasing to prevent magenta glow in flash mode
                        fc.font.setSmooth(false);
                    }
                    LOG_INFO << "Loaded font " << i << ": " << fc.ttfFile << "\n";
                } else {
                    LOG_WARN << "Failed to load font: " << fullPath << "\n";
                }
                
                // Load font styling parameters
                std::string iStr = "font[id=" + std::to_string(i) + "]";
                
                fc.fillColor = parseHexColor("skin.fonts." + iStr + ".color", sf::Color::White);
                
                auto outlineEnabledIt = parameters.find("skin.fonts." + iStr + ".outline.enabled");
                if (outlineEnabledIt != parameters.end()) {
                    fc.outlineEnabled = (outlineEnabledIt->second == "true" || outlineEnabledIt->second == "1");
                }
                
                auto outlineThicknessIt = parameters.find("skin.fonts." + iStr + ".outline.thickness");
                if (outlineThicknessIt != parameters.end()) {
                    try { fc.outlineThickness = std::stof(outlineThicknessIt->second); } catch (...) {}
                }
                
                fc.outlineColor = parseHexColor("skin.fonts." + iStr + ".outline.color", sf::Color::Black);

                if (fc.outlineEnabled) {
                    LOG_INFO << "  Applied outline: enabled=true, thickness=" << fc.outlineThickness 
                             << ", color=" << std::hex << std::uppercase << fc.outlineColor.toInteger() << std::dec << "\n";
                }
                
                fontConfigs.push_back(std::move(fc));
            }
        }
        
        // If no fonts specified, use default
        if (fontConfigs.empty()) {
            FontConfig fc;
            fc.index = 0;
            fc.ttfFile = "times.ttf";
            fc.pcfFile = "times.pcf";
            if (defaultFont.openFromFile("C:/Windows/Fonts/times.ttf")) {
                defaultFont.setSmooth(false);
                fc.font = defaultFont;
                fc.loaded = true;
            }
            fontConfigs.push_back(std::move(fc));
        }
    }
    
    // Load flash configuration
    void loadFlashConfig() {
        flashConfig.enabledLayers = FlashLayer::None;
        
        if (parameters.find("skin.flash.background") != parameters.end() &&
            (parameters["skin.flash.background"] == "true" || parameters["skin.flash.background"] == "1")) {
            flashConfig.enabledLayers = flashConfig.enabledLayers | FlashLayer::Background;
        }
        if (parameters.find("skin.flash.character") != parameters.end() &&
            (parameters["skin.flash.character"] == "true" || parameters["skin.flash.character"] == "1")) {
            flashConfig.enabledLayers = flashConfig.enabledLayers | FlashLayer::Character;
        }
        if (parameters.find("skin.flash.weather_icon") != parameters.end() &&
            (parameters["skin.flash.weather_icon"] == "true" || parameters["skin.flash.weather_icon"] == "1")) {
            flashConfig.enabledLayers = flashConfig.enabledLayers | FlashLayer::WeatherIcon;
        }
        if (parameters.find("skin.flash.text") != parameters.end() &&
            (parameters["skin.flash.text"] == "true" || parameters["skin.flash.text"] == "1")) {
            flashConfig.enabledLayers = flashConfig.enabledLayers | FlashLayer::Text;
        }
        
        // Temperature thresholds
        auto warmIt = parameters.find("skin.character.temp.warm");
        if (warmIt != parameters.end()) {
            try { warmTempThreshold = std::stof(warmIt->second); } catch (...) {}
        }
        auto hotIt = parameters.find("skin.character.temp.hot");
        if (hotIt != parameters.end()) {
            try { hotTempThreshold = std::stof(hotIt->second); } catch (...) {}
        }
    }

public:
    std::string name;
    std::string xmlFilePath;
    bool initialized = false;

    Skin(std::string name, int width, int height) : name(name), DISPLAY_WIDTH(width), DISPLAY_HEIGHT(height) {} 

    int initialize(const std::string& xmlFilePath) {
        this->xmlFilePath = xmlFilePath;
        parameters.clear();
        fontConfigs.clear();
        
        if (!xmlFilePath.empty()) {
            LOG_INFO << "Loading skin from: " << baseSkinDir << "\n";
            baseSkinDir = std::filesystem::path(xmlFilePath).parent_path().string();
            if (parseXMLFile(xmlFilePath, parameters) != 0) {
                LOG_WARN << "Failed to parse XML file: " << xmlFilePath << "\n";
                return 1;
            }
        }

        // Debug: print loaded parameters
        LOG_INFO << "Loaded skin parameters:\n";
        for (const auto& kv : parameters) {
            LOG_INFO << "  " << kv.first << " = " << kv.second << "\n";
        }
        
        loadFlashConfig();
        loadFonts();
        parametersRefreshed = true;
        initialized = true;
        return 0;
    }
    
    // Get font by index
    sf::Font* getFont(int index) {
        for (auto& fc : fontConfigs) {
            if (fc.index == index && fc.loaded) {
                return &fc.font;
            }
        }
        // Return first loaded font as fallback
        for (auto& fc : fontConfigs) {
            if (fc.loaded) return &fc.font;
        }
        return nullptr;
    }
    
    // Get font config by index (for flash export)
    const FontConfig* getFontConfig(int index) const {
        for (const auto& fc : fontConfigs) {
            if (fc.index == index) return &fc;
        }
        return fontConfigs.empty() ? nullptr : &fontConfigs[0];
    }
    
    // Get all font configs (for flash export)
    const std::vector<FontConfig>& getFontConfigs() const { return fontConfigs; }
    
    // Apply font styling (color and outline) to a text object
    // If overrideColor is provided, it takes precedence over the font's fillColor
    void applyFontStyle(sf::Text& text, int fontIndex, const sf::Color* overrideColor = nullptr) const {
        const FontConfig* fc = getFontConfig(fontIndex);
        if (fc) {
            if (overrideColor) {
                text.setFillColor(*overrideColor);
            } else {
                text.setFillColor(fc->fillColor);
            }
            
            // Apply outline if enabled
            if (fc->outlineEnabled && fc->outlineThickness > 0.0f) {
                text.setOutlineColor(fc->outlineColor);
                text.setOutlineThickness(fc->outlineThickness);
            }
        }
    }
    
    // Get flash configuration
    const FlashConfig& getFlashConfig() const { return flashConfig; }
    FlashConfig& getFlashConfig() { return flashConfig; }
    bool hasFlashConfig() const { return flashConfig.enabledLayers != FlashLayer::None; }
    
    // Get base skin directory (for flash export)
    const std::string& getBaseSkinDir() const { return baseSkinDir; }
    
    // Get all parameters (for flash export)
    const std::unordered_map<std::string, std::string>& getParameters() const { return parameters; }
    
    // Get temperature thresholds
    float getWarmTempThreshold() const { return warmTempThreshold; }
    float getHotTempThreshold() const { return hotTempThreshold; }

    // Original draw method - uses internal frame counter
    virtual void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train) = 0;

    // Draw method with explicit animation time (for frame lock support)
    // Default implementation calls the original draw method for backward compatibility
    virtual void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train, double animationTime) {
        draw(texture, stats, weather, train);
    }
    
    // Draw with flash mode support - renders only non-flashed layers
    // flashedLayers: which layers are handled by remote (skip rendering these)
    // transparentColor: color to use for areas that remote will fill
    virtual void drawForFlash(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train, 
                              double animationTime, FlashLayer flashedLayers, sf::Color transparentColor) {
        // Default: just call normal draw (override in derived classes)
        draw(texture, stats, weather, train, animationTime);
    }

    virtual ~Skin() = default;
};
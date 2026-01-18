#include "skin.h"
#include "utils/condition.h"

/*

 [AnimeSkin] - A skin with an animated background and character.

 Parameters:
    - "skin.background.png": Path to the background image. For animation there will be multiple frames named "skin.background.0.png", "skin.background.1.png", etc.
    - "skin.background.animation.enabled": Whether to enable animated background (true/false)
    - "skin.background.animation.speed": Speed of the background animation (frames per second)
    - "skin.background.animation.framecount": Number of frames in the background animation
    - "skin.character.animation.enabled": Whether to enable character animation (true/false)
    - "skin.character.animation.speed": Speed of the character animation (frames per second)
    - "skin.character.animation.framecount": Number of frames in the character animation
    - "skin.character.flip": Whether to flip the character horizontally (true/false)
    - "skin.character.png": Path to the character image
    - "skin.character.x": X position of the character
    - "skin.character.y": Y position of the character
    - "skin.character.bobbing.enabled": Whether to enable bobbing animation for the character (true/false)
    - "skin.character.bobbing.speed": Speed of the bobbing animation
    - "skin.character.bobbing.amplitude": Amplitude of the bobbing animation (default 5.0)
    - "skin.character.warm.png": Path to the character image when CPU is warm
    - "skin.character.hot.png": Path to the character image when CPU is hot
    - "skin.character.temp.warm": Temperature threshold for warm state (default 60.0)
    - "skin.character.temp.hot": Temperature threshold for hot state (default 80.0)
    
    Weather:
    - "skin.weather.icon.sunny.png": Path to sunny weather icon
    - "skin.weather.icon.sunny.animation.enabled": Enable animation (true/false)
    - "skin.weather.icon.sunny.animation.speed": Animation speed (fps)
    - "skin.weather.icon.sunny.animation.framecount": Number of animation frames
    - "skin.weather.icon.rainy.png": Path to rainy weather icon (+ animation params)
    - "skin.weather.icon.thunderstorm.png": Path to thunderstorm weather icon (+ animation params)
    - "skin.weather.icon.cloudy.png": Path to cloudy weather icon (+ animation params)
    - "skin.weather.icon.night.png": Path to night weather icon (+ animation params)
    - "skin.weather.icon.windy.png": Path to windy weather icon (+ animation params)
    - "skin.weather.icon.foggy.png": Path to foggy weather icon (+ animation params)
    - "skin.weather.icon.width": Width of weather icon
    - "skin.weather.icon.height": Height of weather icon
    - "skin.weather.icon.x": X position of weather icon
    - "skin.weather.icon.y": Y position of weather icon
    - "skin.weather.text.fontindex": Index of font to use for weather text
    - "skin.weather.text.x": X position of weather text
    - "skin.weather.text.y": Y position of weather text
    - "skin.weather.text.color": Color of weather text (hex RGB)
    - "skin.weather.text.size": Font size of weather text
    
    Hardware monitor:
    - "skin.hwmon.text.fontindex": Index of font to use for hwmon text
    - "skin.hwmon.cpu.usage.header": Header text for CPU usage
    - "skin.hwmon.cpu.usage.text.x": X position of CPU usage text
    - "skin.hwmon.cpu.usage.text.y": Y position of CPU usage text
    - "skin.hwmon.cpu.usage.text.color": Color of CPU usage text (hex RGB)
    - "skin.hwmon.cpu.usage.text.size": Font size of CPU usage text
    - "skin.hwmon.cpu.usage.icon.path": Path to CPU usage icon
    - "skin.hwmon.cpu.usage.icon.x": X position of CPU usage icon
    - "skin.hwmon.cpu.usage.icon.y": Y position of CPU usage icon
    - "skin.hwmon.cpu.usage.icon.width": Width of CPU usage icon
    - "skin.hwmon.cpu.usage.icon.height": Height of CPU usage icon
    - "skin.hwmon.cpu.temp.header": Header text for CPU temperature
    - "skin.hwmon.cpu.temp.text.x": X position of CPU temperature text
    - "skin.hwmon.cpu.temp.text.y": Y position of CPU temperature text
    - "skin.hwmon.cpu.temp.text.color": Color of CPU temperature text (hex RGB)
    - "skin.hwmon.cpu.temp.text.size": Font size of CPU temperature text
    - "skin.hwmon.cpu.temp.icon.path": Path to CPU temperature icon
    - "skin.hwmon.cpu.temp.icon.x": X position of CPU temperature icon
    - "skin.hwmon.cpu.temp.icon.y": Y position of CPU temperature icon
    - "skin.hwmon.cpu.temp.icon.width": Width of CPU temperature icon
    - "skin.hwmon.cpu.temp.icon.height": Height of CPU temperature icon
    - "skin.hwmon.cpu.combine": Whether to combine CPU usage and temperature into one line
    - "skin.hwmon.cpu.combinedivider": Separator string between usage and temp when combined
    - "skin.hwmon.cpu.pincombinedivider": Pin the position of the combined divider as much as possible (true/false)
    - "skin.hwmon.mem.usage.header": Header text for memory usage
    - "skin.hwmon.mem.usage.text.x": X position of memory usage text
    - "skin.hwmon.mem.usage.text.y": Y position of memory usage text
    - "skin.hwmon.mem.usage.text.color": Color of memory usage text (hex RGB)
    - "skin.hwmon.mem.usage.text.size": Font size of memory usage text
    - "skin.hwmon.mem.usage.icon.path": Path to memory usage icon
    - "skin.hwmon.mem.usage.icon.x": X position of memory usage icon
    - "skin.hwmon.mem.usage.icon.y": Y position of memory usage icon
    - "skin.hwmon.mem.usage.icon.width": Width of memory usage icon
    - "skin.hwmon.mem.usage.icon.height": Height of memory usage icon
    - "skin.hwmon.train.next.header": Header text for next train arrival
    - "skin.hwmon.train.next.text.x": X position of next train text
    - "skin.hwmon.train.next.text.y": Y position of next train text
    - "skin.hwmon.train.next.text.color": Color of next train text (hex RGB)
    - "skin.hwmon.train.next.text.size": Font size of next train text
    - "skin.hwmon.train.next.text.divider": Divider string between train line and arrival time
    - "skin.hwmon.train.next.icon.path": Path to next train icon
    - "skin.hwmon.train.next.icon.x": X position of next train icon
    - "skin.hwmon.train.next.icon.y": Y position of next train icon
    - "skin.hwmon.train.next.icon.width": Width of next train icon
    - "skin.hwmon.train.next.icon.height": Height of next train icon

    Effects
    - "skin.effects.jpegify.enabled": Enable JPEG compression effect on the whole texture (true/false)
    - "skin.effects.jpegify.quality": Quality level for JPEG compression (0-100, default 30)
    - "skin.effects.jpegify.loadinggif": Whether to apply jpegify effect to the loading gif in flash mode (true/false)
    
    Fonts:
    - "skin.fonts.font[id=0].ttf": Path to first font TTF file (corresponding PCF should exist with same name)
    - "skin.fonts.font[id=1].ttf": Path to second font TTF file
    - ... etc
    - "skin.fonts.font[id=N].color": Color of Nth font (hex RGB)
    - "skin.fonts.font[id=N].outline.enabled": Enable font stroke
    - "skin.fonts.font[id=N].outline.thickness": Thickness of outline
    - "skin.fonts.font[id=N].outline.color": Color of outline (hex RGB)
    
    Flash mode:
    - "skin.flash.background": Enable flashing background layer (true/false)
    - "skin.flash.character": Enable flashing character layer (true/false)
    - "skin.flash.weather_icon": Enable flashing weather icon layer (true/false)
    - "skin.flash.text": Enable flashing text layer (true/false)
    - "skin.flash.loading": Enable flashing loading gif (true/false)
*/

// Transparent color key for flash mode (magenta in RGB565 = 0xF81F)
const sf::Color TRANSPARENT_COLOR_KEY(248, 0, 248);

class AnimeSkin : public Skin {
private:
    // Background
    std::vector<sf::Texture> backgroundFrames;
    bool backgroundAnimated = false;
    float backgroundAnimSpeed = 1.0f;
    int backgroundFrameCount = 1;

    // Character - normal state
    std::vector<sf::Texture> characterFrames;
    bool characterAnimated = false;
    float characterAnimSpeed = 1.0f;
    int characterFrameCount = 1;
    
    // Character - warm state
    std::vector<sf::Texture> characterWarmFrames;
    bool characterWarmAnimated = false;
    float characterWarmAnimSpeed = 1.0f;
    int characterWarmFrameCount = 1;
    bool hasCharacterWarm = false;
    
    // Character - hot state
    std::vector<sf::Texture> characterHotFrames;
    bool characterHotAnimated = false;
    float characterHotAnimSpeed = 1.0f;
    int characterHotFrameCount = 1;
    bool hasCharacterHot = false;
    
    // Character common properties
    bool characterFlip = false;
    float characterX = 0;
    float characterY = 0;
    bool characterBobbing = false;
    float characterBobbingSpeed = 1.0f;
    float characterBobbingAmplitude = 5.0f;
    bool hasCharacter = false;

    // Weather icons - animated frames for each type
    std::vector<sf::Texture> weatherIconSunnyFrames;
    std::vector<sf::Texture> weatherIconRainyFrames;
    std::vector<sf::Texture> weatherIconThunderstormFrames;
    std::vector<sf::Texture> weatherIconCloudyFrames;
    std::vector<sf::Texture> weatherIconNightFrames;
    std::vector<sf::Texture> weatherIconWindyFrames;
    std::vector<sf::Texture> weatherIconFoggyFrames;
    bool hasWeatherIconSunny = false;
    bool hasWeatherIconRainy = false;
    bool hasWeatherIconThunderstorm = false;
    bool hasWeatherIconCloudy = false;
    bool hasWeatherIconNight = false;
    bool hasWeatherIconWindy = false;
    bool hasWeatherIconFoggy = false;
    // Animation settings per weather type
    bool weatherIconSunnyAnimated = false;
    bool weatherIconRainyAnimated = false;
    bool weatherIconThunderstormAnimated = false;
    bool weatherIconCloudyAnimated = false;
    bool weatherIconNightAnimated = false;
    bool weatherIconWindyAnimated = false;
    bool weatherIconFoggyAnimated = false;
    float weatherIconSunnyAnimSpeed = 1.0f;
    float weatherIconRainyAnimSpeed = 1.0f;
    float weatherIconThunderstormAnimSpeed = 1.0f;
    float weatherIconCloudyAnimSpeed = 1.0f;
    float weatherIconNightAnimSpeed = 1.0f;
    float weatherIconWindyAnimSpeed = 1.0f;
    float weatherIconFoggyAnimSpeed = 1.0f;
    int weatherIconSunnyFrameCount = 1;
    int weatherIconRainyFrameCount = 1;
    int weatherIconThunderstormFrameCount = 1;
    int weatherIconCloudyFrameCount = 1;
    int weatherIconNightFrameCount = 1;
    int weatherIconWindyFrameCount = 1;
    int weatherIconFoggyFrameCount = 1;
    float weatherIconWidth = 32;
    float weatherIconHeight = 32;
    float weatherIconX = 0;
    float weatherIconY = 0;
    bool hasWeatherIconPosition = false;

    // Weather text
    int weatherTextFontIndex = 0;
    float weatherTextX = 0;
    float weatherTextY = 0;
    sf::Color weatherTextColor = sf::Color::White;
    unsigned int weatherTextSize = 14;
    bool hasWeatherText = false;

    // Hardware monitor
    int hwmonTextFontIndex = 0;
    // Hardware monitor - CPU usage
    std::string cpuUsageHeader = "CPU: ";
    float cpuUsageTextX = 0;
    float cpuUsageTextY = 0;
    sf::Color cpuUsageTextColor = sf::Color::White;
    unsigned int cpuUsageTextSize = 14;
    sf::Texture cpuUsageIcon;
    bool hasCpuUsageIcon = false;
    bool hasCpuUsageText = false;
    float cpuUsageIconX = 0;
    float cpuUsageIconY = 0;
    float cpuUsageIconWidth = 32;
    float cpuUsageIconHeight = 32;

    // Hardware monitor - CPU temp
    std::string cpuTempHeader = "Temp: ";
    float cpuTempTextX = 0;
    float cpuTempTextY = 0;
    sf::Color cpuTempTextColor = sf::Color::White;
    unsigned int cpuTempTextSize = 14;
    sf::Texture cpuTempIcon;
    bool hasCpuTempIcon = false;
    bool hasCpuTempText = false;
    bool cpuCombine = false;
    std::string cpuCombinedDivider = " @ ";
    bool cpuPinCombinedDivider = false;
    float cpuCombinedFixedTextWidth = 0; // Calculated width of combined text for pinning divider
    float cpuTempIconX = 0;
    float cpuTempIconY = 0;
    float cpuTempIconWidth = 32;
    float cpuTempIconHeight = 32;

    // Hardware monitor - Memory usage
    std::string memUsageHeader = "Mem: ";
    float memUsageTextX = 0;
    float memUsageTextY = 0;
    sf::Color memUsageTextColor = sf::Color::White;
    unsigned int memUsageTextSize = 14;
    sf::Texture memUsageIcon;
    bool hasMemUsageIcon = false;
    bool hasMemUsageText = false;
    float memUsageIconX = 0;
    float memUsageIconY = 0;
    float memUsageIconWidth = 32;
    float memUsageIconHeight = 32;

    // Hardware monitor - Train
    std::string trainNextHeader = "Next Train: ";
    float trainNextTextX = 0;
    float trainNextTextY = 0;
    sf::Color trainNextTextColor = sf::Color::White;
    unsigned int trainNextTextSize = 14;
    std::string trainNextTextDivider = " | ";
    sf::Texture trainNextIcon;
    bool hasTrainNextIcon = false;
    bool hasTrainNextText = false;
    float trainNextIconX = 0;
    float trainNextIconY = 0;
    float trainNextIconWidth = 32;
    float trainNextIconHeight = 32;

    bool initialized = false;

    // Helper to get parameter with default
    std::string getParam(const std::string& key, const std::string& defaultVal = "") {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            return it->second;
        }
        LOG_WARN << "Key not found: " << key << "\n";
        return defaultVal;
    }

    bool hasParam(const std::string& key) {
        return parameters.find(key) != parameters.end();
    }

    float getParamFloat(const std::string& key, float defaultVal = 0.0f) {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            try {
                return std::stof(it->second);
            } catch (...) {
                return defaultVal;
            }
        }
        LOG_WARN << "Float key not found or invalid: " << key << "\n";
        return defaultVal;
    }

    int getParamInt(const std::string& key, int defaultVal = 0) {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
                return defaultVal;
            }
        }
        LOG_WARN << "Int key not found or invalid: " << key << "\n";
        return defaultVal;
    }

    bool getParamBool(const std::string& key, bool defaultVal = false) {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            return it->second == "true" || it->second == "1" || it->second == "True";
        }
        LOG_WARN << "Boolean key not found: " << key << "\n";
        return defaultVal;
    }

    std::string getParamString(const std::string& key, const std::string& defaultVal = "") {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            return it->second;
        }
        LOG_WARN << "String key not found: " << key << "\n";
        return defaultVal;
    }

    sf::Color getParamColor(const std::string& key, sf::Color defaultVal = sf::Color::White) {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            std::string hexStr = it->second;
            if (it->second[0] == '#') {
                hexStr = it->second.substr(1);
            }
            try {
                unsigned int hex = std::stoul(hexStr, nullptr, 16);
                return sf::Color((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
            } catch (...) {
                LOG_WARN << "Invalid color for key " << key << ": " << it->second << "\n";
                return defaultVal;
            }
        }
        LOG_WARN << "Color key not found: " << key << "\n";
        return defaultVal;
    }

    // Time-based bobbing offset calculation
    float getBobOffset(double time, float speed, float amplitude) {
        return std::sin(time * speed * 2.0f * 3.14159f) * amplitude;
    }

    // Get weather icon info for animation
    struct WeatherIconInfo {
        std::vector<sf::Texture>* frames = nullptr;
        bool animated = false;
        float animSpeed = 1.0f;
        int frameCount = 1;
    };
    
    WeatherIconInfo getWeatherIconInfo(const WeatherData& weather) {
        WeatherIconInfo info;
        if (!weather.available) return info;

        const std::string& weatherType = getWeatherIconNameSimplified(weather);

        // Map weather codes to icons
        if (weatherType == "sunny" && hasWeatherIconSunny) {
            info.frames = &weatherIconSunnyFrames;
            info.animated = weatherIconSunnyAnimated;
            info.animSpeed = weatherIconSunnyAnimSpeed;
            info.frameCount = weatherIconSunnyFrameCount;
        } else if (weatherType == "cloudy" && hasWeatherIconCloudy) {
            info.frames = &weatherIconCloudyFrames;
            info.animated = weatherIconCloudyAnimated;
            info.animSpeed = weatherIconCloudyAnimSpeed;
            info.frameCount = weatherIconCloudyFrameCount;
        } else if (weatherType == "rainy" && hasWeatherIconRainy) {
            info.frames = &weatherIconRainyFrames;
            info.animated = weatherIconRainyAnimated;
            info.animSpeed = weatherIconRainyAnimSpeed;
            info.frameCount = weatherIconRainyFrameCount;
        } else if (weatherType == "thunderstorm" && hasWeatherIconThunderstorm) {
            info.frames = &weatherIconThunderstormFrames;
            info.animated = weatherIconThunderstormAnimated;
            info.animSpeed = weatherIconThunderstormAnimSpeed;
            info.frameCount = weatherIconThunderstormFrameCount;
        } else if (weatherType == "foggy" && hasWeatherIconFoggy) {
            info.frames = &weatherIconFoggyFrames;
            info.animated = weatherIconFoggyAnimated;
            info.animSpeed = weatherIconFoggyAnimSpeed;
            info.frameCount = weatherIconFoggyFrameCount;
        } else if (weatherType == "windy" && hasWeatherIconWindy) {
            info.frames = &weatherIconWindyFrames;
            info.animated = weatherIconWindyAnimated;
            info.animSpeed = weatherIconWindyAnimSpeed;
            info.frameCount = weatherIconWindyFrameCount;
        } else {
            // Default to day/night if available
            if (weather.isNight && hasWeatherIconNight) {
                info.frames = &weatherIconNightFrames;
                info.animated = weatherIconNightAnimated;
                info.animSpeed = weatherIconNightAnimSpeed;
                info.frameCount = weatherIconNightFrameCount;
            } else if (hasWeatherIconSunny) {
                info.frames = &weatherIconSunnyFrames;
                info.animated = weatherIconSunnyAnimated;
                info.animSpeed = weatherIconSunnyAnimSpeed;
                info.frameCount = weatherIconSunnyFrameCount;
            } else if (hasWeatherIconNight) {
                info.frames = &weatherIconNightFrames;
                info.animated = weatherIconNightAnimated;
                info.animSpeed = weatherIconNightAnimSpeed;
                info.frameCount = weatherIconNightFrameCount;
            }
        }
        
        return info;
    }
    
    sf::Texture* getWeatherIcon(const WeatherData& weather) {
        if (!weather.available) return nullptr;

        const std::string& weatherType = getWeatherIconNameSimplified(weather);

        // Map weather codes to icons (return first frame for compatibility)
        if (weatherType == "sunny") {
            if (hasWeatherIconSunny && !weatherIconSunnyFrames.empty()) return &weatherIconSunnyFrames[0];
        } else if (weatherType == "cloudy") {
            if (hasWeatherIconCloudy && !weatherIconCloudyFrames.empty()) return &weatherIconCloudyFrames[0];
        } else if (weatherType == "rainy") {
            if (hasWeatherIconRainy && !weatherIconRainyFrames.empty()) return &weatherIconRainyFrames[0];
        } else if (weatherType == "thunderstorm") {
            if (hasWeatherIconThunderstorm && !weatherIconThunderstormFrames.empty()) return &weatherIconThunderstormFrames[0];
        } else if (weatherType == "foggy") {
            if (hasWeatherIconFoggy && !weatherIconFoggyFrames.empty()) return &weatherIconFoggyFrames[0];
        }

        // Default to day/night if available
        if (weather.isNight) {
             if (hasWeatherIconNight && !weatherIconNightFrames.empty()) return &weatherIconNightFrames[0];
        } else {
             if (hasWeatherIconSunny && !weatherIconSunnyFrames.empty()) return &weatherIconSunnyFrames[0];
        }
        if (hasWeatherIconNight && !weatherIconNightFrames.empty()) return &weatherIconNightFrames[0];
        return nullptr;
    }
    
    // Get weather icon index for flash mode protocol
    int getWeatherIconIndex(const WeatherData& weather) {
        if (!weather.available) return -1;
        
        const std::string& weatherType = getWeatherIconNameSimplified(weather);
        
        if (weatherType == "sunny") return 0;
        if (weatherType == "cloudy") return 1;
        if (weatherType == "rainy") return 2;
        if (weatherType == "thunderstorm") return 3;
        if (weatherType == "foggy") return 4;
        if (weatherType == "windy") return 5;
        
        // Default based on day/night
        if (weather.isNight) return 6;  // night
        return 0;  // sunny
    }
    
    // Load animation frames helper
    bool loadAnimationFrames(const std::string& basePath, int frameCount, std::vector<sf::Texture>& frames) {
        frames.clear();
        if (frameCount > 1) {
            // Load animated frames: base.0.png, base.1.png, etc.
            std::string pathNoExt = basePath.substr(0, basePath.rfind(".png"));
            for (int i = 0; i < frameCount; i++) {
                sf::Texture tex;
                std::string framePath = pathNoExt + "." + std::to_string(i) + ".png";
                if (tex.loadFromFile(framePath)) {
                    frames.push_back(std::move(tex));
                }
            }
        } else {
            // Load single frame
            sf::Texture tex;
            if (tex.loadFromFile(basePath)) {
                frames.push_back(std::move(tex));
            }
        }
        return !frames.empty();
    }

    void loadResources() {
        if (initialized && !parametersRefreshed) return;
        initialized = true;
        if (parametersRefreshed) {
            LOG_INFO << "Refreshing skin parameters...\n";
            backgroundFrames.clear();
            characterFrames.clear();
            characterWarmFrames.clear();
            characterHotFrames.clear();
            weatherIconSunnyFrames.clear();
            weatherIconRainyFrames.clear();
            weatherIconThunderstormFrames.clear();
            weatherIconCloudyFrames.clear();
            weatherIconNightFrames.clear();
            weatherIconWindyFrames.clear();
            weatherIconFoggyFrames.clear();
        }
        parametersRefreshed = false;

        // Load background frames
        backgroundAnimated = getParamBool("skin.background.animation.enabled", false);
        backgroundAnimSpeed = getParamFloat("skin.background.animation.speed", 1.0f);
        backgroundFrameCount = getParamInt("skin.background.animation.framecount", 1);

        std::string bgPath = baseSkinDir + "/" + getParam("skin.background.png");
        if (!bgPath.empty()) {
            if (backgroundAnimated && backgroundFrameCount > 1) {
                loadAnimationFrames(bgPath, backgroundFrameCount, backgroundFrames);
            } else {
                sf::Texture tex;
                if (tex.loadFromFile(bgPath)) {
                    backgroundFrames.push_back(std::move(tex));
                }
            }
        }

        // Character common properties
        characterFlip = getParamBool("skin.character.flip", false);
        characterX = getParamFloat("skin.character.x", 0);
        characterY = getParamFloat("skin.character.y", 0);
        characterBobbing = getParamBool("skin.character.bobbing.enabled", false);
        characterBobbingSpeed = getParamFloat("skin.character.bobbing.speed", 1.0f);
        characterBobbingAmplitude = getParamFloat("skin.character.bobbing.amplitude", 5.0f);

        // Load character frames - normal state
        characterAnimated = getParamBool("skin.character.animation.enabled", false);
        characterAnimSpeed = getParamFloat("skin.character.animation.speed", 1.0f);
        characterFrameCount = getParamInt("skin.character.animation.framecount", 1);

        std::string charPath = baseSkinDir + "/" + getParam("skin.character.png");
        if (!charPath.empty()) {
            if (characterAnimated && characterFrameCount > 1) {
                loadAnimationFrames(charPath, characterFrameCount, characterFrames);
            } else {
                sf::Texture tex;
                if (tex.loadFromFile(charPath)) {
                    characterFrames.push_back(std::move(tex));
                }
            }
            hasCharacter = !characterFrames.empty();
        }

        // Load character frames - warm state
        std::string charWarmPath = getParam("skin.character.warm.png");
        if (!charWarmPath.empty()) {
            charWarmPath = baseSkinDir + "/" + charWarmPath;
            characterWarmAnimated = getParamBool("skin.character.warm.animation.enabled", characterAnimated);
            characterWarmAnimSpeed = getParamFloat("skin.character.warm.animation.speed", characterAnimSpeed);
            characterWarmFrameCount = getParamInt("skin.character.warm.animation.framecount", characterFrameCount);
            
            if (characterWarmAnimated && characterWarmFrameCount > 1) {
                hasCharacterWarm = loadAnimationFrames(charWarmPath, characterWarmFrameCount, characterWarmFrames);
            } else {
                sf::Texture tex;
                if (tex.loadFromFile(charWarmPath)) {
                    characterWarmFrames.push_back(std::move(tex));
                    hasCharacterWarm = true;
                }
            }
        }

        // Load character frames - hot state
        std::string charHotPath = getParam("skin.character.hot.png");
        if (!charHotPath.empty()) {
            charHotPath = baseSkinDir + "/" + charHotPath;
            characterHotAnimated = getParamBool("skin.character.hot.animation.enabled", characterAnimated);
            characterHotAnimSpeed = getParamFloat("skin.character.hot.animation.speed", characterAnimSpeed);
            characterHotFrameCount = getParamInt("skin.character.hot.animation.framecount", characterFrameCount);
            
            if (characterHotAnimated && characterHotFrameCount > 1) {
                hasCharacterHot = loadAnimationFrames(charHotPath, characterHotFrameCount, characterHotFrames);
            } else {
                sf::Texture tex;
                if (tex.loadFromFile(charHotPath)) {
                    characterHotFrames.push_back(std::move(tex));
                    hasCharacterHot = true;
                }
            }
        }

        // Load weather icons
        // Helper lambda to load weather icon frames
        auto loadWeatherIcon = [&](const std::string& keyBase, 
                                   std::vector<sf::Texture>& frames,
                                   bool& hasIcon, bool& animated, 
                                   float& animSpeed, int& frameCount) {
            std::string iconPath = getParam(keyBase + ".png");
            if (iconPath.empty()) return;
            
            std::string fullPath = baseSkinDir + "/" + iconPath;
            animated = getParamBool(keyBase + ".animation.enabled", false);
            animSpeed = getParamFloat(keyBase + ".animation.speed", 1.0f);
            frameCount = getParamInt(keyBase + ".animation.framecount", 1);
            
            if (animated && frameCount > 1) {
                hasIcon = loadAnimationFrames(fullPath, frameCount, frames);
            } else {
                sf::Texture tex;
                if (tex.loadFromFile(fullPath)) {
                    frames.push_back(std::move(tex));
                    hasIcon = true;
                }
            }
        };
        
        loadWeatherIcon("skin.weather.icon.sunny", weatherIconSunnyFrames, 
                        hasWeatherIconSunny, weatherIconSunnyAnimated, 
                        weatherIconSunnyAnimSpeed, weatherIconSunnyFrameCount);
        loadWeatherIcon("skin.weather.icon.rainy", weatherIconRainyFrames,
                        hasWeatherIconRainy, weatherIconRainyAnimated,
                        weatherIconRainyAnimSpeed, weatherIconRainyFrameCount);
        loadWeatherIcon("skin.weather.icon.thunderstorm", weatherIconThunderstormFrames,
                        hasWeatherIconThunderstorm, weatherIconThunderstormAnimated,
                        weatherIconThunderstormAnimSpeed, weatherIconThunderstormFrameCount);
        loadWeatherIcon("skin.weather.icon.cloudy", weatherIconCloudyFrames,
                        hasWeatherIconCloudy, weatherIconCloudyAnimated,
                        weatherIconCloudyAnimSpeed, weatherIconCloudyFrameCount);
        loadWeatherIcon("skin.weather.icon.night", weatherIconNightFrames,
                        hasWeatherIconNight, weatherIconNightAnimated,
                        weatherIconNightAnimSpeed, weatherIconNightFrameCount);
        loadWeatherIcon("skin.weather.icon.windy", weatherIconWindyFrames,
                        hasWeatherIconWindy, weatherIconWindyAnimated,
                        weatherIconWindyAnimSpeed, weatherIconWindyFrameCount);
        loadWeatherIcon("skin.weather.icon.foggy", weatherIconFoggyFrames,
                        hasWeatherIconFoggy, weatherIconFoggyAnimated,
                        weatherIconFoggyAnimSpeed, weatherIconFoggyFrameCount);

        weatherIconWidth = getParamFloat("skin.weather.icon.width", 32);
        weatherIconHeight = getParamFloat("skin.weather.icon.height", 32);
        weatherIconX = getParamFloat("skin.weather.icon.x", 0);
        weatherIconY = getParamFloat("skin.weather.icon.y", 0);
        hasWeatherIconPosition = hasParam("skin.weather.icon.x") && hasParam("skin.weather.icon.y");

        // Weather text
        weatherTextFontIndex = getParamInt("skin.weather.text.fontindex", 0);
        weatherTextX = getParamFloat("skin.weather.text.x", 0);
        weatherTextY = getParamFloat("skin.weather.text.y", 0);
        weatherTextColor = getParamColor("skin.weather.text.color", sf::Color::White);
        weatherTextSize = getParamInt("skin.weather.text.size", 14);
        hasWeatherText = hasParam("skin.weather.text.x") && hasParam("skin.weather.text.y");

        // Hardware monitor font
        hwmonTextFontIndex = getParamInt("skin.hwmon.text.fontindex", 0);

        std::string iconPath;
        
        // CPU usage
        cpuUsageHeader = getParam("skin.hwmon.cpu.usage.header", "CPU: ");
        cpuUsageTextX = getParamFloat("skin.hwmon.cpu.usage.text.x", 0);
        cpuUsageTextY = getParamFloat("skin.hwmon.cpu.usage.text.y", 0);
        cpuUsageTextColor = getParamColor("skin.hwmon.cpu.usage.text.color", sf::Color::White);
        cpuUsageTextSize = getParamInt("skin.hwmon.cpu.usage.text.size", 14);
        hasCpuUsageText = hasParam("skin.hwmon.cpu.usage.text.x") && hasParam("skin.hwmon.cpu.usage.text.y");
        
        iconPath = getParam("skin.hwmon.cpu.usage.icon.path");
        if (!iconPath.empty()) hasCpuUsageIcon = cpuUsageIcon.loadFromFile(baseSkinDir + "/" + iconPath);
        cpuUsageIconX = getParamFloat("skin.hwmon.cpu.usage.icon.x", 0);
        cpuUsageIconY = getParamFloat("skin.hwmon.cpu.usage.icon.y", 0);
        cpuUsageIconWidth = getParamFloat("skin.hwmon.cpu.usage.icon.width", 32);
        cpuUsageIconHeight = getParamFloat("skin.hwmon.cpu.usage.icon.height", 32);

        // CPU temp
        cpuTempHeader = getParam("skin.hwmon.cpu.temp.header", "Temp: ");
        cpuTempTextX = getParamFloat("skin.hwmon.cpu.temp.text.x", 0);
        cpuTempTextY = getParamFloat("skin.hwmon.cpu.temp.text.y", 0);
        cpuTempTextColor = getParamColor("skin.hwmon.cpu.temp.text.color", sf::Color::White);
        cpuTempTextSize = getParamInt("skin.hwmon.cpu.temp.text.size", 14);
        hasCpuTempText = hasParam("skin.hwmon.cpu.temp.text.x") && hasParam("skin.hwmon.cpu.temp.text.y");
        cpuCombine = getParamBool("skin.hwmon.cpu.combine", false);
        cpuCombinedDivider = getParamString("skin.hwmon.cpu.combinedivider", " @ ");
        cpuPinCombinedDivider = getParamBool("skin.hwmon.cpu.pincombinedivider", false);
        // Calculate combined text width if needed for pinning divider
        if (cpuCombine && cpuPinCombinedDivider && hasCpuUsageText) {
            // Calculate the width of each glyph
            cpuCombinedFixedTextWidth = 0;
            sf::Font* hwmonFont = getFont(hwmonTextFontIndex);
            for (char c : cpuUsageHeader) {
                sf::Glyph glyph = hwmonFont->getGlyph(c, cpuUsageTextSize, false);
                cpuCombinedFixedTextWidth += glyph.advance;
            }
        }
        
        iconPath = getParam("skin.hwmon.cpu.temp.icon.path");
        if (!iconPath.empty()) hasCpuTempIcon = cpuTempIcon.loadFromFile(baseSkinDir + "/" + iconPath);
        cpuTempIconX = getParamFloat("skin.hwmon.cpu.temp.icon.x", 0);
        cpuTempIconY = getParamFloat("skin.hwmon.cpu.temp.icon.y", 0);
        cpuTempIconWidth = getParamFloat("skin.hwmon.cpu.temp.icon.width", 32);
        cpuTempIconHeight = getParamFloat("skin.hwmon.cpu.temp.icon.height", 32);

        // Memory usage
        memUsageHeader = getParam("skin.hwmon.mem.usage.header", "Mem: ");
        memUsageTextX = getParamFloat("skin.hwmon.mem.usage.text.x", 0);
        memUsageTextY = getParamFloat("skin.hwmon.mem.usage.text.y", 0);
        memUsageTextColor = getParamColor("skin.hwmon.mem.usage.text.color", sf::Color::White);
        memUsageTextSize = getParamInt("skin.hwmon.mem.usage.text.size", 14);
        hasMemUsageText = hasParam("skin.hwmon.mem.usage.text.x") && hasParam("skin.hwmon.mem.usage.text.y");
        
        iconPath = getParam("skin.hwmon.mem.usage.icon.path");
        if (!iconPath.empty()) hasMemUsageIcon = memUsageIcon.loadFromFile(baseSkinDir + "/" + iconPath);
        memUsageIconX = getParamFloat("skin.hwmon.mem.usage.icon.x", 0);
        memUsageIconY = getParamFloat("skin.hwmon.mem.usage.icon.y", 0);
        memUsageIconWidth = getParamFloat("skin.hwmon.mem.usage.icon.width", 32);
        memUsageIconHeight = getParamFloat("skin.hwmon.mem.usage.icon.height", 32);

        // Train
        trainNextHeader = getParam("skin.hwmon.train.next.header", "Next Train: ");
        trainNextTextX = getParamFloat("skin.hwmon.train.next.text.x", 0);
        trainNextTextY = getParamFloat("skin.hwmon.train.next.text.y", 0);
        trainNextTextColor = getParamColor("skin.hwmon.train.next.text.color", sf::Color::White);
        trainNextTextSize = getParamInt("skin.hwmon.train.next.text.size", 14);
        trainNextTextDivider = getParamString("skin.hwmon.train.next.text.divider", " | ");
        hasTrainNextText = hasParam("skin.hwmon.train.next.text.x") && hasParam("skin.hwmon.train.next.text.y");
        
        iconPath = getParam("skin.hwmon.train.next.icon.path");
        if (!iconPath.empty()) hasTrainNextIcon = trainNextIcon.loadFromFile(baseSkinDir + "/" + iconPath);
        trainNextIconX = getParamFloat("skin.hwmon.train.next.icon.x", 0);
        trainNextIconY = getParamFloat("skin.hwmon.train.next.icon.y", 0);
        trainNextIconWidth = getParamFloat("skin.hwmon.train.next.icon.width", 32);
        trainNextIconHeight = getParamFloat("skin.hwmon.train.next.icon.height", 32);
    }
    
    struct CharacterFrameInfo {
        sf::Texture* texture = nullptr;
        bool animated = false;
        float animSpeed = 1.0f;
        int frameCount = 1;
        std::vector<sf::Texture>* frames = nullptr;
    };
    
    // Get character texture and frame info based on temperature state
    CharacterFrameInfo getCharacterFrameInfo(CharacterTempState state) {
        CharacterFrameInfo info;
        
        switch (state) {
            case CharacterTempState::Hot:
                if (hasCharacterHot) {
                    info.frames = &characterHotFrames;
                    info.animated = characterHotAnimated;
                    info.animSpeed = characterHotAnimSpeed;
                    info.frameCount = characterHotFrameCount;
                    return info;
                }
                // Fall through to warm if no hot texture
                [[fallthrough]];
                
            case CharacterTempState::Warm:
                if (hasCharacterWarm) {
                    info.frames = &characterWarmFrames;
                    info.animated = characterWarmAnimated;
                    info.animSpeed = characterWarmAnimSpeed;
                    info.frameCount = characterWarmFrameCount;
                    return info;
                }
                // Fall through to normal if no warm texture
                [[fallthrough]];
                
            case CharacterTempState::Normal:
            default:
                if (hasCharacter) {
                    info.frames = &characterFrames;
                    info.animated = characterAnimated;
                    info.animSpeed = characterAnimSpeed;
                    info.frameCount = characterFrameCount;
                }
                return info;
        }
    }

    // Internal draw implementation that takes explicit animation time and optional layer filtering
    void drawWithTime(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train, 
                      double animTime, FlashLayer skipLayers = FlashLayer::None, sf::Color bgColor = sf::Color::Black) {
        loadResources();

        texture.clear(bgColor);

        // Draw background
        bool skipBackground = hasLayer(skipLayers, FlashLayer::Background);
        if (!skipBackground && !backgroundFrames.empty()) {
            int bgFrame = 0;
            if (backgroundAnimated && backgroundFrames.size() > 1) {
                bgFrame = (int)(animTime * backgroundAnimSpeed) % (int)backgroundFrames.size();
            }
            sf::Sprite bgSprite(backgroundFrames[bgFrame]);
            
            sf::Vector2u texSize = backgroundFrames[bgFrame].getSize();
            bgSprite.setScale(sf::Vector2f(
                (float)DISPLAY_WIDTH / texSize.x,
                (float)DISPLAY_HEIGHT / texSize.y
            ));
            texture.draw(bgSprite);
        }

        // Draw character
        bool skipCharacter = hasLayer(skipLayers, FlashLayer::Character);
        if (!skipCharacter && hasCharacter) {
            CharacterTempState tempState = getCharacterTempState(stats.cpuTempC);
            CharacterFrameInfo charInfo = getCharacterFrameInfo(tempState);
            
            if (charInfo.frames && !charInfo.frames->empty()) {
                int charFrame = 0;
                if (charInfo.animated && charInfo.frames->size() > 1) {
                    charFrame = (int)(animTime * charInfo.animSpeed) % (int)charInfo.frames->size();
                }
                sf::Texture* charTex = &(*charInfo.frames)[charFrame];
                
                sf::Sprite charSprite(*charTex);
                
                float posX = characterX;
                float posY = characterY;
                
                if (characterBobbing) {
                    posY += getBobOffset(animTime, characterBobbingSpeed, characterBobbingAmplitude);
                }

                if (characterFlip) {
                    sf::Vector2u texSize = charTex->getSize();
                    charSprite.setScale(sf::Vector2f(-1.0f, 1.0f));
                    charSprite.setPosition(sf::Vector2f(posX + texSize.x, posY));
                } else {
                    charSprite.setPosition(sf::Vector2f(posX, posY));
                }
                
                texture.draw(charSprite);
            }
        }

        // Draw weather icon
        bool skipWeatherIcon = hasLayer(skipLayers, FlashLayer::WeatherIcon);
        if (!skipWeatherIcon && weather.available && hasWeatherIconPosition) {
            WeatherIconInfo iconInfo = getWeatherIconInfo(weather);
            if (iconInfo.frames && !iconInfo.frames->empty()) {
                int frame = 0;
                if (iconInfo.animated && iconInfo.frames->size() > 1) {
                    frame = (int)(animTime * iconInfo.animSpeed) % (int)iconInfo.frames->size();
                }
                sf::Texture* weatherTex = &(*iconInfo.frames)[frame];
                sf::Sprite weatherSprite(*weatherTex);
                
                sf::Vector2u texSize = weatherTex->getSize();
                weatherSprite.setScale(sf::Vector2f(
                    weatherIconWidth / texSize.x,
                    weatherIconHeight / texSize.y
                ));
                weatherSprite.setPosition(sf::Vector2f(weatherIconX, weatherIconY));
                texture.draw(weatherSprite);
            }
        }

        // Draw text elements (skip if text layer is flashed)
        bool skipText = hasLayer(skipLayers, FlashLayer::Text);
        
        // Draw weather text
        if (!skipText && weather.available && hasWeatherText) {
            sf::Font* weatherFont = Skin::getFont(weatherTextFontIndex);
            if (weatherFont) {
                char weatherStr[64];
                snprintf(weatherStr, sizeof(weatherStr), "%.0f\u00B0F", weather.currentTemp);
                sf::Text weatherText(*weatherFont, weatherStr, weatherTextSize);
                weatherText.setPosition(sf::Vector2f(weatherTextX, weatherTextY));
                applyFontStyle(weatherText, weatherTextFontIndex, &weatherTextColor);
                texture.draw(weatherText);
            }
        }

        // Draw CPU usage icon
        if (!skipText && hasCpuUsageIcon) {
            sf::Sprite iconSprite(cpuUsageIcon);
            sf::Vector2u texSize = cpuUsageIcon.getSize();
            iconSprite.setScale(sf::Vector2f(
                cpuUsageIconWidth / texSize.x,
                cpuUsageIconHeight / texSize.y
            ));
            iconSprite.setPosition(sf::Vector2f(cpuUsageIconX, cpuUsageIconY));
            texture.draw(iconSprite);
        }

        // Draw CPU usage text
        if (!skipText && hasCpuUsageText) {
            sf::Font* hwmonFont = Skin::getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char cpuStr[64];
                if (cpuCombine) {
                    if (cpuPinCombinedDivider) {
                        // Use 2 separate strings to pin the divider in place and prevent shifting when temp changes
                        snprintf(cpuStr, sizeof(cpuStr), "%s%.0f%%", cpuUsageHeader.c_str(), stats.cpuPercent);
                        char cpuTempStr[32];
                        snprintf(cpuTempStr, sizeof(cpuTempStr), "%s%.0f\u00B0C", cpuCombinedDivider.c_str(), stats.cpuTempC);
                        sf::Text cpuText(*hwmonFont, cpuStr, cpuUsageTextSize);
                        cpuText.setPosition(sf::Vector2f(cpuUsageTextX, cpuUsageTextY));
                        applyFontStyle(cpuText, hwmonTextFontIndex, &cpuUsageTextColor);
                        texture.draw(cpuText);
                        sf::Text cpuTempText(*hwmonFont, cpuTempStr, cpuUsageTextSize);
                        float cpuTextWidth = cpuCombinedFixedTextWidth;
                        cpuTextWidth += hwmonFont->getGlyph('%', cpuUsageTextSize, false).advance;
                        cpuTextWidth += hwmonFont->getGlyph('2', cpuUsageTextSize, false).advance * (stats.cpuPercent >= 10.0f ? (stats.cpuPercent >= 100.0f ? 3 : 2) : 1); // Account for extra digits if percent > 10 or 100
                        cpuTempText.setPosition(sf::Vector2f(cpuUsageTextX + cpuTextWidth, cpuUsageTextY)); // Position temp text after "CPU: XX%"
                        applyFontStyle(cpuTempText, hwmonTextFontIndex, &cpuUsageTextColor);
                        texture.draw(cpuTempText);
                        goto Skip;
                    } else {
                        snprintf(cpuStr, sizeof(cpuStr), "%s%.0f%%%s%.0f\u00B0C", cpuUsageHeader.c_str(), stats.cpuPercent, cpuCombinedDivider.c_str(), stats.cpuTempC);
                    }
                } else {
                    snprintf(cpuStr, sizeof(cpuStr), "%s%.0f%%", cpuUsageHeader.c_str(), stats.cpuPercent);
                }
                sf::Text cpuText(*hwmonFont, cpuStr, cpuUsageTextSize);
                cpuText.setPosition(sf::Vector2f(cpuUsageTextX, cpuUsageTextY));
                applyFontStyle(cpuText, hwmonTextFontIndex, &cpuUsageTextColor);
                texture.draw(cpuText);
            }
        }
        Skip:

        // Draw CPU temp icon (only if not combined)
        if (!skipText && hasCpuTempIcon && !cpuCombine) {
            sf::Sprite iconSprite(cpuTempIcon);
            sf::Vector2u texSize = cpuTempIcon.getSize();
            iconSprite.setScale(sf::Vector2f(
                cpuTempIconWidth / texSize.x,
                cpuTempIconHeight / texSize.y
            ));
            iconSprite.setPosition(sf::Vector2f(cpuTempIconX, cpuTempIconY));
            texture.draw(iconSprite);
        }

        // Draw CPU temp text (only if not combined)
        if (!skipText && hasCpuTempText && !cpuCombine) {
            sf::Font* hwmonFont = Skin::getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char tempStr[64];
                snprintf(tempStr, sizeof(tempStr), "%s%.0f\u00B0C", cpuTempHeader.c_str(), stats.cpuTempC);
                sf::Text tempText(*hwmonFont, tempStr, cpuTempTextSize);
                tempText.setPosition(sf::Vector2f(cpuTempTextX, cpuTempTextY));
                applyFontStyle(tempText, hwmonTextFontIndex, &cpuTempTextColor);
                texture.draw(tempText);
            }
        }

        // Draw memory usage icon
        if (!skipText && hasMemUsageIcon) {
            sf::Sprite iconSprite(memUsageIcon);
            sf::Vector2u texSize = memUsageIcon.getSize();
            iconSprite.setScale(sf::Vector2f(
                memUsageIconWidth / texSize.x,
                memUsageIconHeight / texSize.y
            ));
            iconSprite.setPosition(sf::Vector2f(memUsageIconX, memUsageIconY));
            texture.draw(iconSprite);
        }

        // Draw memory usage text
        if (!skipText && hasMemUsageText) {
            sf::Font* hwmonFont = Skin::getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char memStr[64];
                snprintf(memStr, sizeof(memStr), "%s%.0f%%", memUsageHeader.c_str(), stats.memPercent);
                sf::Text memText(*hwmonFont, memStr, memUsageTextSize);
                memText.setPosition(sf::Vector2f(memUsageTextX, memUsageTextY));
                applyFontStyle(memText, hwmonTextFontIndex, &memUsageTextColor);
                texture.draw(memText);
            }
        }

        // Draw train icon
        if (!skipText && hasTrainNextIcon && (train.available0 || train.available1)) {
            sf::Sprite iconSprite(trainNextIcon);
            sf::Vector2u texSize = trainNextIcon.getSize();
            iconSprite.setScale(sf::Vector2f(
                trainNextIconWidth / texSize.x,
                trainNextIconHeight / texSize.y
            ));
            iconSprite.setPosition(sf::Vector2f(trainNextIconX, trainNextIconY));
            texture.draw(iconSprite);
        }

        // Draw train text
        if (!skipText && (train.available0 || train.available1) && hasTrainNextText) {
            sf::Font* hwmonFont = Skin::getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char trainStr[128];
                std::string train0Str = (train.available0 && train.minsToNextTrain0 != 999) ? std::to_string((int)train.minsToNextTrain0) + "m" : "--";
                std::string train1Str = (train.available1 && train.minsToNextTrain1 != 999) ? std::to_string((int)train.minsToNextTrain1) + "m" : "--";
                snprintf(trainStr, sizeof(trainStr), "%s%s%s%s", trainNextHeader.c_str(), train0Str.c_str(), trainNextTextDivider.c_str(), train1Str.c_str());
                sf::Text trainText(*hwmonFont, trainStr, trainNextTextSize);
                trainText.setPosition(sf::Vector2f(trainNextTextX, trainNextTextY));
                applyFontStyle(trainText, hwmonTextFontIndex, &trainNextTextColor);
                texture.draw(trainText);
            }
        }

        texture.display();
        
        // Post-processing
        if (jpegifyEffect.isEnabled()) {
            jpegifyEffect.apply(texture);
        }
    }

public:
    AnimeSkin(std::string name, int width, int height) : Skin(name, width, height) {}

    // Original draw method - uses internal frame counter for backward compatibility
    void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train) override {
        double animTime = frameCount / 60.0;
        drawWithTime(texture, stats, weather, train, animTime);
        frameCount++;
    }

    // Draw method with explicit animation time (for frame lock support)
    void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train, double animationTime) override {
        drawWithTime(texture, stats, weather, train, animationTime);
    }
    
    // Draw with flash mode support - renders only non-flashed layers
    void drawForFlash(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train, 
                      double animationTime, FlashLayer flashedLayers, sf::Color transparentColor) override {
        drawWithTime(texture, stats, weather, train, animationTime, flashedLayers, transparentColor);
    }
    
    // Accessors for flash export
    bool hasBackgroundAnimation() const { return backgroundAnimated && backgroundFrameCount > 1; }
    int getBackgroundFrameCount() const { return backgroundFrameCount; }
    float getBackgroundAnimSpeed() const { return backgroundAnimSpeed; }
    
    bool hasCharacterAnimation() const { return characterAnimated && characterFrameCount > 1; }
    int getCharacterFrameCount() const { return characterFrameCount; }
    float getCharacterAnimSpeed() const { return characterAnimSpeed; }
    
    float getCharacterX() const { return characterX; }
    float getCharacterY() const { return characterY; }
    bool getCharacterFlip() const { return characterFlip; }
    bool getCharacterBobbing() const { return characterBobbing; }
    float getCharacterBobbingSpeed() const { return characterBobbingSpeed; }
    float getCharacterBobbingAmplitude() const { return characterBobbingAmplitude; }
    
    bool hasCharacterWarmState() const { return hasCharacterWarm; }
    bool hasCharacterHotState() const { return hasCharacterHot; }
    
    float getWeatherIconX() const { return weatherIconX; }
    float getWeatherIconY() const { return weatherIconY; }
    float getWeatherIconWidth() const { return weatherIconWidth; }
    float getWeatherIconHeight() const { return weatherIconHeight; }
};
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
    - "skin.character.hot.png": Path to the character image when CPU is hot
    - "skin.weather.icon.sunny.png": Path to sunny weather icon
    - "skin.weather.icon.rainy.png": Path to rainy weather icon
    - "skin.weather.icon.thunderstorm.png": Path to thunderstorm weather icon
    - "skin.weather.icon.cloudy.png": Path to cloudy weather icon
    - "skin.weather.icon.night.png": Path to night weather icon
    - "skin.weather.icon.windy.png": Path to windy weather icon
    - "skin.weather.icon.foggy.png": Path to foggy weather icon
    - "skin.weather.icon.width": Width of weather icon
    - "skin.weather.icon.height": Height of weather icon
    - "skin.weather.icon.x": X position of weather icon
    - "skin.weather.icon.y": Y position of weather icon
    - "skin.weather.icon.bobbing.enabled": Whether to enable bobbing animation for weather icon (true/false)
    - "skin.weather.icon.bobbing.speed": Speed of bobbing animation
    - "skin.weather.text.fontindex": Index of font to use for weather text
    - "skin.weather.text.x": X position of weather text
    - "skin.weather.text.y": Y position of weather text
    - "skin.weather.text.color": Color of weather text (hex RGB)
    - "skin.weather.text.size": Font size of weather text
    - "skin.hwmon.text.fontindex": Index of font to use for hwmon text
    - "skin.hwmon.cpu.usage.text.x": X position of CPU usage text
    - "skin.hwmon.cpu.usage.text.y": Y position of CPU usage text
    - "skin.hwmon.cpu.usage.text.color": Color of CPU usage text (hex RGB)
    - "skin.hwmon.cpu.usage.text.size": Font size of CPU usage text
    - "skin.hwmon.cpu.usage.icon.path": Path to CPU usage icon
    - "skin.hwmon.cpu.temp.text.x": X position of CPU temperature text
    - "skin.hwmon.cpu.temp.text.y": Y position of CPU temperature text
    - "skin.hwmon.cpu.temp.text.color": Color of CPU temperature text (hex RGB)
    - "skin.hwmon.cpu.temp.text.size": Font size of CPU temperature text
    - "skin.hwmon.cpu.temp.icon.path": Path to CPU temperature icon
    - "skin.hwmon.cpu.combine": Whether to combine CPU usage and temperature into one line
    - "skin.hwmon.mem.usage.text.x": X position of memory usage text
    - "skin.hwmon.mem.usage.text.y": Y position of memory usage text
    - "skin.hwmon.mem.usage.text.color": Color of memory usage text (hex RGB)
    - "skin.hwmon.mem.usage.text.size": Font size of memory usage text
    - "skin.hwmon.mem.usage.icon.path": Path to memory usage icon
    - "skin.hwmon.train.next.text.x": X position of next train text
    - "skin.hwmon.train.next.text.y": Y position of next train text
    - "skin.hwmon.train.next.text.color": Color of next train text (hex RGB)
    - "skin.hwmon.train.next.text.size": Font size of next train text
    - "skin.hwmon.train.next.icon.path": Path to next train icon
*/

class AnimeSkin : public Skin {
private:
    // Background
    std::vector<sf::Texture> backgroundFrames;
    bool backgroundAnimated = false;
    float backgroundAnimSpeed = 1.0f;
    int backgroundFrameCount = 1;

    // Character
    std::vector<sf::Texture> characterFrames;
    sf::Texture characterHotTexture;
    bool characterAnimated = false;
    float characterAnimSpeed = 1.0f;
    int characterFrameCount = 1;
    bool characterFlip = false;
    float characterX = 0;
    float characterY = 0;
    bool characterBobbing = false;
    float characterBobbingSpeed = 1.0f;
    bool hasCharacter = false;
    bool hasCharacterHot = false;

    // Weather icons
    sf::Texture weatherIconSunny;
    sf::Texture weatherIconRainy;
    sf::Texture weatherIconThunderstorm;
    sf::Texture weatherIconCloudy;
    sf::Texture weatherIconNight;
    sf::Texture weatherIconWindy;
    sf::Texture weatherIconFoggy;
    bool hasWeatherIconSunny = false;
    bool hasWeatherIconRainy = false;
    bool hasWeatherIconThunderstorm = false;
    bool hasWeatherIconCloudy = false;
    bool hasWeatherIconNight = false;
    bool hasWeatherIconWindy = false;
    bool hasWeatherIconFoggy = false;
    float weatherIconWidth = 32;
    float weatherIconHeight = 32;
    float weatherIconX = 0;
    float weatherIconY = 0;
    bool weatherIconBobbing = false;
    float weatherIconBobbingSpeed = 1.0f;
    bool hasWeatherIconPosition = false;

    // Weather text
    int weatherTextFontIndex = 0;
    float weatherTextX = 0;
    float weatherTextY = 0;
    sf::Color weatherTextColor = sf::Color::White;
    unsigned int weatherTextSize = 14;
    bool hasWeatherText = false;

    // Hardware monitor - CPU usage
    int hwmonTextFontIndex = 0;
    float cpuUsageTextX = 0;
    float cpuUsageTextY = 0;
    sf::Color cpuUsageTextColor = sf::Color::White;
    unsigned int cpuUsageTextSize = 14;
    sf::Texture cpuUsageIcon;
    bool hasCpuUsageIcon = false;
    bool hasCpuUsageText = false;

    // Hardware monitor - CPU temp
    float cpuTempTextX = 0;
    float cpuTempTextY = 0;
    sf::Color cpuTempTextColor = sf::Color::White;
    unsigned int cpuTempTextSize = 14;
    sf::Texture cpuTempIcon;
    bool hasCpuTempIcon = false;
    bool hasCpuTempText = false;
    bool cpuCombine = false;

    // Hardware monitor - Memory usage
    float memUsageTextX = 0;
    float memUsageTextY = 0;
    sf::Color memUsageTextColor = sf::Color::White;
    unsigned int memUsageTextSize = 14;
    sf::Texture memUsageIcon;
    bool hasMemUsageIcon = false;
    bool hasMemUsageText = false;

    // Hardware monitor - Train
    float trainNextTextX = 0;
    float trainNextTextY = 0;
    sf::Color trainNextTextColor = sf::Color::White;
    unsigned int trainNextTextSize = 14;
    sf::Texture trainNextIcon;
    bool hasTrainNextIcon = false;
    bool hasTrainNextText = false;

    bool initialized = false;

    // Helper to get parameter with default
    std::string getParam(const std::string& key, const std::string& defaultVal = "") {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            return it->second;
        }
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
        return defaultVal;
    }

    bool getParamBool(const std::string& key, bool defaultVal = false) {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            return it->second == "true" || it->second == "1" || it->second == "True";
        }
        return defaultVal;
    }

    sf::Color getParamColor(const std::string& key, sf::Color defaultVal = sf::Color::White) {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            try {
                unsigned int hex = std::stoul(it->second, nullptr, 16);
                return sf::Color((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
            } catch (...) {
                return defaultVal;
            }
        }
        return defaultVal;
    }

    float getBobOffset(float speed, float amplitude = 5.0f) {
        float time = frameCount / 60.0f; // Assuming ~60 FPS
        return std::sin(time * speed * 2.0f * 3.14159f) * amplitude;
    }

    sf::Texture* getWeatherIcon(const WeatherData& weather) {
        if (!weather.available) return nullptr;

        const std::string& weatherType = getWeatherIconNameSimplified(weather);

        // Map weather codes to icons
        // OpenWeatherMap codes: 01=clear, 02=few clouds, 03=scattered clouds, 
        // 04=broken clouds, 09=shower rain, 10=rain, 11=thunderstorm, 
        // 13=snow, 50=mist
        if (weatherType == "sunny") {
            if (hasWeatherIconSunny) return &weatherIconSunny;
        } else if (weatherType == "cloudy") {
            if (hasWeatherIconCloudy) return &weatherIconCloudy;
        } else if (weatherType == "rainy") {
            if (hasWeatherIconRainy) return &weatherIconRainy;
        } else if (weatherType == "thunderstorm") {
            if (hasWeatherIconThunderstorm) return &weatherIconThunderstorm;
        } else if (weatherType == "foggy") {
            if (hasWeatherIconFoggy) return &weatherIconFoggy;
        }

        // Default to day/night if available
        if (weather.isNight) {
             if (hasWeatherIconNight) return &weatherIconNight;
        } else {
             if (hasWeatherIconSunny) return &weatherIconSunny;
        }
        if (hasWeatherIconNight) return &weatherIconNight;
        return nullptr;
    }

    void loadResources() {
        if (initialized) return;
        initialized = true;

        // Load background frames
        backgroundAnimated = getParamBool("skin.background.animation.enabled", false);
        backgroundAnimSpeed = getParamFloat("skin.background.animation.speed", 1.0f);
        backgroundFrameCount = getParamInt("skin.background.animation.framecount", 1);

        std::string bgPath = baseSkinDir + "/" + getParam("skin.background.png");
        if (!bgPath.empty()) {
            if (backgroundAnimated && backgroundFrameCount > 1) {
                // Load animated frames: skin.background.0.png, skin.background.1.png, etc.
                std::string basePath = bgPath.substr(0, bgPath.rfind(".png"));
                for (int i = 0; i < backgroundFrameCount; i++) {
                    sf::Texture tex;
                    std::string framePath = basePath + "." + std::to_string(i) + ".png";
                    if (tex.loadFromFile(framePath)) {
                        backgroundFrames.push_back(std::move(tex));
                    }
                }
            } else {
                // Load single background
                sf::Texture tex;
                if (tex.loadFromFile(bgPath)) {
                    backgroundFrames.push_back(std::move(tex));
                }
            }
        }

        // Load character frames
        characterAnimated = getParamBool("skin.character.animation.enabled", false);
        characterAnimSpeed = getParamFloat("skin.character.animation.speed", 1.0f);
        characterFrameCount = getParamInt("skin.character.animation.framecount", 1);
        characterFlip = getParamBool("skin.character.flip", false);
        characterX = getParamFloat("skin.character.x", 0);
        characterY = getParamFloat("skin.character.y", 0);
        characterBobbing = getParamBool("skin.character.bobbing.enabled", false);
        characterBobbingSpeed = getParamFloat("skin.character.bobbing.speed", 1.0f);

        std::string charPath = baseSkinDir + "/" + getParam("skin.character.png");
        if (!charPath.empty()) {
            if (characterAnimated && characterFrameCount > 1) {
                std::string basePath = charPath.substr(0, charPath.rfind(".png"));
                for (int i = 0; i < characterFrameCount; i++) {
                    sf::Texture tex;
                    std::string framePath = basePath + "." + std::to_string(i) + ".png";
                    if (tex.loadFromFile(framePath)) {
                        characterFrames.push_back(std::move(tex));
                    }
                }
            } else {
                sf::Texture tex;
                if (tex.loadFromFile(charPath)) {
                    characterFrames.push_back(std::move(tex));
                }
            }
            hasCharacter = !characterFrames.empty();
        }

        std::string charHotPath = getParam("skin.character.hot.png");
        if (!charHotPath.empty()) {
            hasCharacterHot = characterHotTexture.loadFromFile(charHotPath);
        }

        // Load weather icons
        std::string iconPath;
        iconPath = getParam("skin.weather.icon.sunny.png");
        if (!iconPath.empty()) hasWeatherIconSunny = weatherIconSunny.loadFromFile( baseSkinDir + "/" + iconPath);
        
        iconPath = getParam("skin.weather.icon.rainy.png");
        if (!iconPath.empty()) hasWeatherIconRainy = weatherIconRainy.loadFromFile(baseSkinDir + "/" + iconPath);
        
        iconPath = getParam("skin.weather.icon.thunderstorm.png");
        if (!iconPath.empty()) hasWeatherIconThunderstorm = weatherIconThunderstorm.loadFromFile(baseSkinDir + "/" + iconPath);
        
        iconPath = getParam("skin.weather.icon.cloudy.png");
        if (!iconPath.empty()) hasWeatherIconCloudy = weatherIconCloudy.loadFromFile(baseSkinDir + "/" + iconPath);
        
        iconPath = getParam("skin.weather.icon.night.png");
        if (!iconPath.empty()) hasWeatherIconNight = weatherIconNight.loadFromFile(baseSkinDir + "/" + iconPath);
        
        iconPath = getParam("skin.weather.icon.windy.png");
        if (!iconPath.empty()) hasWeatherIconWindy = weatherIconWindy.loadFromFile(baseSkinDir + "/" + iconPath);
        
        iconPath = getParam("skin.weather.icon.foggy.png");
        if (!iconPath.empty()) hasWeatherIconFoggy = weatherIconFoggy.loadFromFile(baseSkinDir + "/" + iconPath);

        weatherIconWidth = getParamFloat("skin.weather.icon.width", 32);
        weatherIconHeight = getParamFloat("skin.weather.icon.height", 32);
        weatherIconX = getParamFloat("skin.weather.icon.x", 0);
        weatherIconY = getParamFloat("skin.weather.icon.y", 0);
        weatherIconBobbing = getParamBool("skin.weather.icon.bobbing.enabled", false);
        weatherIconBobbingSpeed = getParamFloat("skin.weather.icon.bobbing.speed", 1.0f);
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

        // CPU usage
        cpuUsageTextX = getParamFloat("skin.hwmon.cpu.usage.text.x", 0);
        cpuUsageTextY = getParamFloat("skin.hwmon.cpu.usage.text.y", 0);
        cpuUsageTextColor = getParamColor("skin.hwmon.cpu.usage.text.color", sf::Color::White);
        cpuUsageTextSize = getParamInt("skin.hwmon.cpu.usage.text.size", 14);
        hasCpuUsageText = hasParam("skin.hwmon.cpu.usage.text.x") && hasParam("skin.hwmon.cpu.usage.text.y");
        
        iconPath = getParam("skin.hwmon.cpu.usage.icon.path");
        if (!iconPath.empty()) hasCpuUsageIcon = cpuUsageIcon.loadFromFile(iconPath);

        // CPU temp
        cpuTempTextX = getParamFloat("skin.hwmon.cpu.temp.text.x", 0);
        cpuTempTextY = getParamFloat("skin.hwmon.cpu.temp.text.y", 0);
        cpuTempTextColor = getParamColor("skin.hwmon.cpu.temp.text.color", sf::Color::White);
        cpuTempTextSize = getParamInt("skin.hwmon.cpu.temp.text.size", 14);
        hasCpuTempText = hasParam("skin.hwmon.cpu.temp.text.x") && hasParam("skin.hwmon.cpu.temp.text.y");
        cpuCombine = getParamBool("skin.hwmon.cpu.combine", false);
        
        iconPath = getParam("skin.hwmon.cpu.temp.icon.path");
        if (!iconPath.empty()) hasCpuTempIcon = cpuTempIcon.loadFromFile(iconPath);

        // Memory usage
        memUsageTextX = getParamFloat("skin.hwmon.mem.usage.text.x", 0);
        memUsageTextY = getParamFloat("skin.hwmon.mem.usage.text.y", 0);
        memUsageTextColor = getParamColor("skin.hwmon.mem.usage.text.color", sf::Color::White);
        memUsageTextSize = getParamInt("skin.hwmon.mem.usage.text.size", 14);
        hasMemUsageText = hasParam("skin.hwmon.mem.usage.text.x") && hasParam("skin.hwmon.mem.usage.text.y");
        
        iconPath = getParam("skin.hwmon.mem.usage.icon.path");
        if (!iconPath.empty()) hasMemUsageIcon = memUsageIcon.loadFromFile(iconPath);

        // Train
        trainNextTextX = getParamFloat("skin.hwmon.train.next.text.x", 0);
        trainNextTextY = getParamFloat("skin.hwmon.train.next.text.y", 0);
        trainNextTextColor = getParamColor("skin.hwmon.train.next.text.color", sf::Color::White);
        trainNextTextSize = getParamInt("skin.hwmon.train.next.text.size", 14);
        hasTrainNextText = hasParam("skin.hwmon.train.next.text.x") && hasParam("skin.hwmon.train.next.text.y");
        
        iconPath = getParam("skin.hwmon.train.next.icon.path");
        if (!iconPath.empty()) hasTrainNextIcon = trainNextIcon.loadFromFile(iconPath);
    }

    sf::Font* getFont(int index) {
        if (index >= 0 && index < (int)fonts.size()) {
            return fonts[index];
        }
        return fonts.empty() ? nullptr : fonts[0];
    }

public:
    AnimeSkin(std::string name, int width, int height) : Skin(name, width, height) {}

    void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train) override {
        loadResources();

        texture.clear(sf::Color::Black);

        // Draw background
        if (!backgroundFrames.empty()) {
            int bgFrame = 0;
            if (backgroundAnimated && backgroundFrames.size() > 1) {
                float framesPerTick = backgroundAnimSpeed / 60.0f;
                bgFrame = (int)(frameCount * framesPerTick) % (int)backgroundFrames.size();
            }
            sf::Sprite bgSprite(backgroundFrames[bgFrame]);
            
            // Scale to fit display
            sf::Vector2u texSize = backgroundFrames[bgFrame].getSize();
            bgSprite.setScale(sf::Vector2f(
                (float)DISPLAY_WIDTH / texSize.x,
                (float)DISPLAY_HEIGHT / texSize.y
            ));
            texture.draw(bgSprite);
        }

        // Draw character
        if (hasCharacter) {
            // Determine if we should use hot texture
            bool useHot = hasCharacterHot && isCpuHot(stats.cpuTempC);
            
            sf::Texture* charTex = nullptr;
            if (useHot) {
                charTex = &characterHotTexture;
            } else if (!characterFrames.empty()) {
                int charFrame = 0;
                if (characterAnimated && characterFrames.size() > 1) {
                    float framesPerTick = characterAnimSpeed / 60.0f;
                    charFrame = (int)(frameCount * framesPerTick) % (int)characterFrames.size();
                }
                charTex = &characterFrames[charFrame];
            }

            if (charTex) {
                sf::Sprite charSprite(*charTex);
                
                float posX = characterX;
                float posY = characterY;
                
                if (characterBobbing) {
                    posY += getBobOffset(characterBobbingSpeed);
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
        if (weather.available && hasWeatherIconPosition) {
            sf::Texture* weatherTex = getWeatherIcon(weather);
            if (weatherTex) {
                sf::Sprite weatherSprite(*weatherTex);
                
                float posX = weatherIconX;
                float posY = weatherIconY;
                
                if (weatherIconBobbing) {
                    posY += getBobOffset(weatherIconBobbingSpeed);
                }

                // Scale to specified size
                sf::Vector2u texSize = weatherTex->getSize();
                weatherSprite.setScale(sf::Vector2f(
                    weatherIconWidth / texSize.x,
                    weatherIconHeight / texSize.y
                ));
                weatherSprite.setPosition(sf::Vector2f(posX, posY));
                texture.draw(weatherSprite);
            }
        }

        // Draw weather text
        if (weather.available && hasWeatherText) {
            sf::Font* weatherFont = getFont(weatherTextFontIndex);
            if (weatherFont) {
                char weatherStr[64];
                snprintf(weatherStr, sizeof(weatherStr), "%.0f°F", weather.currentTemp);
                sf::Text weatherText(*weatherFont, weatherStr, weatherTextSize);
                weatherText.setPosition(sf::Vector2f(weatherTextX, weatherTextY));
                weatherText.setFillColor(weatherTextColor);
                texture.draw(weatherText);
            }
        }

        // Draw CPU usage text
        if (hasCpuUsageText) {
            sf::Font* hwmonFont = getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char cpuStr[64];
                if (cpuCombine) {
                    snprintf(cpuStr, sizeof(cpuStr), "CPU: %.0f%% / %.0f°C", stats.cpuPercent, stats.cpuTempC);
                } else {
                    snprintf(cpuStr, sizeof(cpuStr), "CPU: %.0f%%", stats.cpuPercent);
                }
                sf::Text cpuText(*hwmonFont, cpuStr, cpuUsageTextSize);
                cpuText.setPosition(sf::Vector2f(cpuUsageTextX, cpuUsageTextY));
                cpuText.setFillColor(cpuUsageTextColor);
                texture.draw(cpuText);
            }
        }

        // Draw CPU temp text (only if not combined)
        if (hasCpuTempText && !cpuCombine) {
            sf::Font* hwmonFont = getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char tempStr[64];
                snprintf(tempStr, sizeof(tempStr), "Temp: %.0f°C", stats.cpuTempC);
                sf::Text tempText(*hwmonFont, tempStr, cpuTempTextSize);
                tempText.setPosition(sf::Vector2f(cpuTempTextX, cpuTempTextY));
                tempText.setFillColor(cpuTempTextColor);
                texture.draw(tempText);
            }
        }

        // Draw memory usage text
        if (hasMemUsageText) {
            sf::Font* hwmonFont = getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char memStr[64];
                snprintf(memStr, sizeof(memStr), "Mem: %.0f%%", stats.memPercent);
                sf::Text memText(*hwmonFont, memStr, memUsageTextSize);
                memText.setPosition(sf::Vector2f(memUsageTextX, memUsageTextY));
                memText.setFillColor(memUsageTextColor);
                texture.draw(memText);
            }
        }

        // Draw train text
        if ((train.available0 || train.available1) && hasTrainNextText) {
            sf::Font* hwmonFont = getFont(hwmonTextFontIndex);
            if (hwmonFont) {
                char trainStr[128];
                if (train.available0 && train.available1) {
                    snprintf(trainStr, sizeof(trainStr), "%s: %.0fm | %s: %.0fm",
                            train.headsign0.c_str(), train.minsToNextTrain0,
                            train.headsign1.c_str(), train.minsToNextTrain1);
                } else if (train.available0) {
                    snprintf(trainStr, sizeof(trainStr), "%s: %.0fm",
                            train.headsign0.c_str(), train.minsToNextTrain0);
                } else {
                    snprintf(trainStr, sizeof(trainStr), "%s: %.0fm",
                            train.headsign1.c_str(), train.minsToNextTrain1);
                }
                sf::Text trainText(*hwmonFont, trainStr, trainNextTextSize);
                trainText.setPosition(sf::Vector2f(trainNextTextX, trainNextTextY));
                trainText.setFillColor(trainNextTextColor);
                texture.draw(trainText);
            }
        }

        frameCount++;
        texture.display();
    }
};

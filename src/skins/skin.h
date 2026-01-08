#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <unordered_map>

#include "../system_stats.h"
#include "../utils/xml.h"
#include "../weather.hpp"
#include "../train.hpp"

class Skin {
private:
    
protected:
    sf::Font font;
    const int DISPLAY_WIDTH;
    const int DISPLAY_HEIGHT;
    std::unordered_map<std::string, std::string> parameters;
    std::vector<sf::Font*> fonts;
    unsigned long frameCount = 0;
    std::string baseSkinDir;
    bool parametersRefreshed = false;

public:
    std::string name;
    std::string xmlFilePath;

    Skin(std::string name, int width, int height) : name(name), DISPLAY_WIDTH(width), DISPLAY_HEIGHT(height) {} 

    int initialize(const std::string& xmlFilePath) {
        this->xmlFilePath = xmlFilePath;
        parameters.clear();
        fonts.clear();
        if (!font.openFromFile("C:/Windows/Fonts/times.ttf")) {
            std::cerr << "Failed to load font\n";
            return 1;
        }
        fonts.push_back(&font);
        if (!xmlFilePath.empty()) {
            baseSkinDir = std::filesystem::path(xmlFilePath).parent_path().string();
            std::cout << "Loading skin from: " << baseSkinDir << "\n";
            if (parseXMLFile(xmlFilePath, parameters) != 0) {
                std::cerr << "Failed to parse XML file: " << xmlFilePath << "\n";
                return 1;
            }
        }
        parametersRefreshed = true;
        return 0;
    }

    virtual void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather, TrainData& train) = 0;

    virtual ~Skin() = default;
};

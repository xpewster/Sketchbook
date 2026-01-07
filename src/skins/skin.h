#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <unordered_map>

#include "../system_stats.h"
#include "../utils/xml.h"
#include "../weather.hpp"

class Skin {
private:
    std::string name;

protected:
    sf::Font font;
    const int DISPLAY_WIDTH;
    const int DISPLAY_HEIGHT;
    std::unordered_map<std::string, std::string> parameters;
    std::vector<sf::Font*> fonts;
    unsigned long frameCount = 0;

public:

    Skin(std::string name, int width, int height) : name(name), DISPLAY_WIDTH(width), DISPLAY_HEIGHT(height) {}

    int initialize(const std::string& xmlFilePath) {
        if (!font.openFromFile("C:/Windows/Fonts/times.ttf")) {
            std::cerr << "Failed to load font\n";
            return 1;
        }
        fonts.push_back(&font);
        if (!xmlFilePath.empty()) {
            if (parseXMLFile(xmlFilePath, parameters) != 0) {
                std::cerr << "Failed to parse XML file: " << xmlFilePath << "\n";
                return 1;
            }
        }
        return 0;
    }

    virtual void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather) = 0;

    virtual ~Skin() = default;
};

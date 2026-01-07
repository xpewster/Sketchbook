#pragma once

#include <SFML/Graphics.hpp>
#include <string>

#include "../system_stats.h"

class Skin {
private:
    std::string name;

protected:
    sf::Font font;
    const int DISPLAY_WIDTH;
    const int DISPLAY_HEIGHT;

public:

    Skin(std::string name, int width, int height) : name(name), DISPLAY_WIDTH(width), DISPLAY_HEIGHT(height) {}

    int initialize(const std::string& xmlFilePath) {
        if (!font.openFromFile("C:/Windows/Fonts/times.ttf")) {
            std::cerr << "Failed to load font\n";
            return 1;
        }
        return 0;
    }

    virtual void draw(sf::RenderTexture& texture, SystemStats& stats) = 0;

    virtual ~Skin() = default;
};

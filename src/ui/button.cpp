#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <iostream>
#include <optional>

class Button {
public:
    sf::RectangleShape shape;
    sf::Text text;
    sf::Color normalColor;
    sf::Color hoverColor;
    sf::Texture iconTex;
    std::optional<sf::Sprite> icon;
    
    Button(float x, float y, float w, float h, const std::string& label, sf::Font& font)
        : text(font, label, 14) {
        shape.setPosition(sf::Vector2f(x, y));
        shape.setSize(sf::Vector2f(w, h));
        shape.setOutlineThickness(1);
        shape.setOutlineColor(sf::Color(80, 80, 80));
        
        normalColor = sf::Color(200, 200, 200);
        hoverColor = sf::Color(220, 220, 220);
        shape.setFillColor(normalColor);
        
        text.setFillColor(sf::Color::Black);
        centerText();
    }
    
    void setLabel(const std::string& label) {
        text.setString(label);
        centerText();
    }

    void setLabelColor(sf::Color color) {
        text.setFillColor(color);
    }
    
    void setColor(sf::Color normal, sf::Color hover) {
        normalColor = normal;
        hoverColor = hover;
    }

    void setIcon(const std::string& filepath, float x, float y, float w, float h) {
        if (!iconTex.loadFromFile(filepath)) {
            return; 
        }
        icon.emplace(iconTex);
        sf::Vector2f pos = shape.getPosition();
        icon->setScale(sf::Vector2f(w / iconTex.getSize().x, h / iconTex.getSize().y));
        icon->setPosition(sf::Vector2f(pos.x + x, pos.y + y));
    }
    
    bool update(sf::Vector2i mousePos, bool mousePressed, sf::RenderWindow& window) {
        bool hover = shape.getGlobalBounds().contains(window.mapPixelToCoords(mousePos));
        shape.setFillColor(hover ? hoverColor : normalColor);
        return hover && mousePressed;
    }
    
    void draw(sf::RenderWindow& window) {
        window.draw(shape);
        window.draw(text);
        if (icon.has_value()) {
            window.draw(*icon);
        }
    }

    sf::Vector2f getSize() const {
        return shape.getSize();
    }

private:
    void centerText() {
        sf::FloatRect textBounds = text.getLocalBounds();
        sf::Vector2f pos = shape.getPosition();
        sf::Vector2f size = shape.getSize();
        text.setPosition(sf::Vector2f(
            pos.x + (size.x - textBounds.size.x) / 2 - textBounds.position.x,
            pos.y + (size.y - textBounds.size.y) / 2 - textBounds.position.y
        ));
    }
};

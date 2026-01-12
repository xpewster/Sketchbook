#pragma once

#include <SFML/Graphics.hpp>
#include <string>

class TextInput {
public:
    sf::RectangleShape box;
    sf::Text text;
    std::string value;
    bool focused = false;
    size_t cursorPos = 0;
    sf::Clock cursorBlinkClock;
    
    TextInput(float x, float y, float w, float h, const std::string& initial, sf::Font& font)
        : text(font, initial, 14), value(initial), cursorPos(initial.size()) {
        box.setPosition(sf::Vector2f(x, y));
        box.setSize(sf::Vector2f(w, h));
        box.setFillColor(sf::Color::White);
        box.setOutlineColor(sf::Color(100, 100, 100));
        box.setOutlineThickness(1);
        
        text.setFillColor(sf::Color::Black);
        text.setPosition(sf::Vector2f(x + 4, y + 4));
    }
    
    void handleEvent(const sf::Event& event, sf::Vector2i mousePos, sf::RenderWindow& window) {
        if (const auto* buttonPressed = event.getIf<sf::Event::MouseButtonPressed>()) {
            const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
            focused = box.getGlobalBounds().contains(mousePosF);
            box.setOutlineColor(focused ? sf::Color::Blue : sf::Color(100, 100, 100));
            
            if (focused) {
                // Find cursor position based on click - start from beginning
                cursorPos = 0;
                for (size_t i = 0; i <= value.size(); ++i) {
                    float charX = text.findCharacterPos(i).x;
                    if (mousePosF.x < charX - 4) {
                        break;
                    }
                    cursorPos = i;
                }
                cursorBlinkClock.restart();
            }
        }
        
        if (focused) {
            if (const auto* textEntered = event.getIf<sf::Event::TextEntered>()) {
                if (textEntered->unicode == '\b') {
                    // Backspace at cursor position
                    if (cursorPos > 0) {
                        value.erase(cursorPos - 1, 1);
                        cursorPos--;
                    }
                } else if (textEntered->unicode >= 32 && textEntered->unicode < 127) {
                    // Insert at cursor position
                    value.insert(cursorPos, 1, static_cast<char>(textEntered->unicode));
                    cursorPos++;
                }
                text.setString(value);
                cursorBlinkClock.restart();
            }
            
            // Handle arrow keys for cursor movement
            if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->code == sf::Keyboard::Key::Left && cursorPos > 0) {
                    cursorPos--;
                    cursorBlinkClock.restart();
                } else if (keyPressed->code == sf::Keyboard::Key::Right && cursorPos < value.size()) {
                    cursorPos++;
                    cursorBlinkClock.restart();
                }
            }
        }
    }

    bool update(sf::Vector2i mousePos, sf::RenderWindow& window) {
        bool hover = box.getGlobalBounds().contains(window.mapPixelToCoords(mousePos));
        box.setOutlineColor(hover ? sf::Color(135, 135, 135) : sf::Color(100, 100, 100));
        return hover;
    }
    
    void draw(sf::RenderWindow& window) {
        window.draw(box);
        window.draw(text);
        
        // Draw blinking cursor when focused
        if (focused && (int)(cursorBlinkClock.getElapsedTime().asSeconds() * 2) % 2 == 0) {
            sf::RectangleShape cursor(sf::Vector2f(1, text.getCharacterSize()));
            const auto characterPos = text.findCharacterPos(cursorPos);
            cursor.setPosition(sf::Vector2f(characterPos.x, characterPos.y + 1));
            cursor.setFillColor(sf::Color::Black);
            window.draw(cursor);
        }
    }
};

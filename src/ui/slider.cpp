#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <functional>
#include <cmath>
#include <sstream>
#include <iomanip>

class Slider {
public:
    sf::RectangleShape track;
    sf::RectangleShape fill;
    sf::RectangleShape thumb;
    sf::Text valueText;

    int minValue;
    int maxValue;
    int currentValue;
    bool dragging = false;
    bool showValue = true;
    int decimalPlaces = 1;

    std::function<void(int)> onChange;

    sf::Vector2f position;
    sf::Vector2f size;

    static constexpr float THUMB_W = 10.f;
    static constexpr float THUMB_H = 18.f;
    static constexpr float TRACK_H = 4.f;

    Slider(float x, float y, float w, int minVal, int maxVal,
           int defaultVal, sf::Font& font)
        : minValue(minVal), maxValue(maxVal), currentValue(defaultVal),
          position(x, y), size(w, THUMB_H), valueText(font, "", 12) {

        float trackY = y + (THUMB_H - TRACK_H) / 2.f;

        track.setPosition(sf::Vector2f(x, trackY));
        track.setSize(sf::Vector2f(w, TRACK_H));
        track.setFillColor(sf::Color(180, 180, 180));
        track.setOutlineColor(sf::Color(130, 130, 130));
        track.setOutlineThickness(1);

        fill.setPosition(sf::Vector2f(x, trackY));
        fill.setSize(sf::Vector2f(0, TRACK_H));
        fill.setFillColor(sf::Color(252, 186, 3));

        thumb.setSize(sf::Vector2f(THUMB_W, THUMB_H));
        thumb.setFillColor(sf::Color(220, 220, 220));
        thumb.setOutlineColor(sf::Color(80, 80, 80));
        thumb.setOutlineThickness(1);

        valueText.setFillColor(sf::Color(60, 60, 60));

        setValue(defaultVal);
    }

    void setShowValue(bool show) {
        showValue = show;
    }

    void setDecimalPlaces(int places) {
        decimalPlaces = places;
        setValue(currentValue); // refresh label
    }

    void setTrackColor(sf::Color color) {
        track.setFillColor(color);
    }

    void setFillColor(sf::Color color) {
        fill.setFillColor(color);
    }

    void setThumbColor(sf::Color normal) {
        thumb.setFillColor(normal);
    }

    void setOnChange(std::function<void(int)> callback) {
        onChange = callback;
    }

    void setValue(int value) {
        currentValue = std::clamp(value, minValue, maxValue);
        updateThumbAndFill();
        updateLabel();
    }

    int getValue() const { return currentValue; }

    bool isDragging() const { return dragging; }

    void handleEvent(const sf::Event& event, sf::Vector2i mousePos, sf::RenderWindow& window) {
        const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);

        if (const auto* pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
            if (pressed->button == sf::Mouse::Button::Left) {
                if (thumb.getGlobalBounds().contains(mousePosF) ||
                    track.getGlobalBounds().contains(mousePosF)) {
                    dragging = true;
                    setFromMouseX(mousePosF.x);
                }
            }
        }

        if (const auto* released = event.getIf<sf::Event::MouseButtonReleased>()) {
            if (released->button == sf::Mouse::Button::Left) {
                dragging = false;
            }
        }

        if (dragging) {
            if (const auto* moved = event.getIf<sf::Event::MouseMoved>()) {
                setFromMouseX(mousePosF.x);
            }
        }
    }

    bool update(sf::Vector2i mousePos, sf::RenderWindow& window) {
        const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
        bool hover = thumb.getGlobalBounds().contains(mousePosF) ||
                     track.getGlobalBounds().contains(mousePosF);

        sf::Color thumbColor = dragging   ? sf::Color(195, 195, 210)
                             : hover      ? sf::Color(235, 235, 235)
                                          : sf::Color(220, 220, 220);
        thumb.setFillColor(thumbColor);
        return hover;
    }

    void draw(sf::RenderWindow& window) {
        window.draw(track);
        window.draw(fill);
        window.draw(thumb);
        if (showValue) {
            window.draw(valueText);
        }
    }

    sf::Vector2f getSize() const { return size; }

private:
    void setFromMouseX(float mouseX) {
        float trackLeft  = position.x + THUMB_W / 2.f;
        float trackRight = position.x + size.x - THUMB_W / 2.f;
        float t = (mouseX - trackLeft) / (trackRight - trackLeft);
        t = std::clamp(t, 0.f, 1.f);

        int newValue = static_cast<int>(minValue + t * (maxValue - minValue));
        if (newValue != currentValue) {
            currentValue = newValue;
            updateThumbAndFill();
            updateLabel();
            if (onChange) onChange(currentValue);
        }
    }

    void updateThumbAndFill() {
        float t = (maxValue > minValue)
            ? static_cast<float>(currentValue - minValue) / static_cast<float>(maxValue - minValue)
            : 0.f;

        float trackLeft  = position.x + THUMB_W / 2.f;
        float trackRight = position.x + size.x - THUMB_W / 2.f;
        float thumbX = trackLeft + t * (trackRight - trackLeft) - THUMB_W / 2.f;

        thumb.setPosition(sf::Vector2f(thumbX, position.y));
        fill.setSize(sf::Vector2f(thumbX + THUMB_W / 2.f - position.x, TRACK_H));
    }

    void updateLabel() {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(decimalPlaces) << currentValue;
        valueText.setString(oss.str());

        // Position label centered above the thumb
        sf::FloatRect tb = valueText.getLocalBounds();
        sf::FloatRect thumbBounds = thumb.getGlobalBounds();
        valueText.setPosition(sf::Vector2f(
            thumbBounds.position.x + thumbBounds.size.x / 2.f - tb.size.x / 2.f - tb.position.x,
            position.y + tb.size.y + tb.position.y + 8.f
        ));
    }
};
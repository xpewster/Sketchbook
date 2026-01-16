#include <SFML/Graphics.hpp>
#include <string>
#include <optional>

class Checkbox {
private:
    sf::RectangleShape box;
    sf::Text labelText;
    bool checked = false;
    bool hovered = false;
    
    sf::Font& font;
    sf::Vector2f position;
    float boxSize;
    
    sf::Texture checkmarkTex;
    std::optional<sf::Sprite> checkmarkSprite;

    bool disabled = false;

    bool wasJustUpdated_ = false; // Flag to track if the checkbox was just checked in the current event loop

public:

    Checkbox(float x, float y, float size, const std::string& label, 
             sf::Font& f, float label_offset_x, float label_offset_y, bool defaultChecked = false)
        : checked(defaultChecked), font(f), position(x, y), 
          boxSize(size), labelText(f, label, 14) {
        
        box.setPosition(sf::Vector2f(x, y));
        box.setSize(sf::Vector2f(size, size));
        box.setFillColor(sf::Color::White);
        box.setOutlineColor(sf::Color(100, 100, 100));
        box.setOutlineThickness(1);
        
        labelText.setFillColor(sf::Color::Black);
        labelText.setPosition(sf::Vector2f(x + size + label_offset_x, y + label_offset_y));

        // Load a default checkmark icon
        if (checkmarkTex.loadFromFile("resources/CheckMark.png")) {
            checkmarkSprite.emplace(checkmarkTex);
            
            // Scale to fit inside the box with some padding
            float padding = size * 0.0f;
            float targetSize = size - (padding * 2);
            sf::Vector2u texSize = checkmarkTex.getSize();
            checkmarkSprite->setScale(sf::Vector2f(
                targetSize / texSize.x,
                targetSize / texSize.y
            ));
            
            checkmarkSprite->setPosition(sf::Vector2f(
                position.x + padding,
                position.y + padding
            ));
        }
    }
    
    // void setCheckmarkIcon(const std::string& filepath) {
    //     if (!checkmarkTex.loadFromFile(filepath)) {
    //         return;
    //     }
    //     checkmarkSprite.emplace(checkmarkTex);
        
    //     // Scale to fit inside the box with some padding
    //     float padding = boxSize * 0.15f;
    //     float targetSize = boxSize - (padding * 2);
    //     sf::Vector2u texSize = checkmarkTex.getSize();
    //     checkmarkSprite->setScale(sf::Vector2f(
    //         targetSize / texSize.x,
    //         targetSize / texSize.y
    //     ));
        
    //     checkmarkSprite->setPosition(sf::Vector2f(
    //         position.x + padding,
    //         position.y + padding
    //     ));
    // }
    
    void handleEvent(const sf::Event& event, sf::Vector2i mousePos, sf::RenderWindow& window) {
        if (disabled) {
            return;
        }
        const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
        
        if (const auto* buttonPressed = event.getIf<sf::Event::MouseButtonPressed>()) {
            if (box.getGlobalBounds().contains(mousePosF)) {
                checked = !checked;
                wasJustUpdated_ = true;
            }
        }
        
        if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
            hovered = box.getGlobalBounds().contains(mousePosF);
        }
    }
    
    void draw(sf::RenderWindow& window) {
        // Update box appearance based on hover state
        if (disabled) {
            box.setFillColor(sf::Color(220, 220, 220));
        } else if (hovered) {
            box.setFillColor(sf::Color(240, 240, 240));
        } else {
            box.setFillColor(sf::Color::White);
        }
        
        window.draw(box);
        window.draw(labelText);
        
        // Draw checkmark sprite if checked and icon is loaded
        if (checked && checkmarkSprite.has_value()) {
            window.draw(*checkmarkSprite);
        }
    }
    
    bool isChecked() const { return checked; }
    void setChecked(bool value, bool updateEvent = false) { checked = value; if (updateEvent) { wasJustUpdated_ = true; } }

    // Returns if there was just an update event and consumes the event.
    bool wasJustUpdated() {
        if (wasJustUpdated_) {
            wasJustUpdated_ = false;
            return true;
        }
        return false;
    }

    void setDisabled(bool isDisabled) {
        disabled = isDisabled;
    }

    void setLabelColor(sf::Color color) {
        labelText.setFillColor(color);
    }
    
    void setLabel(const std::string& label) {
        labelText.setString(label);
    }

    void setPosition(float x, float y) {
        position = sf::Vector2f(x, y);
        sf::Vector2 posChange = position - box.getPosition();
        box.setPosition(position);
        labelText.setPosition(labelText.getPosition() + posChange);
        if (checkmarkSprite.has_value()) {
            sf::Vector2f oldPos = checkmarkSprite->getPosition();
            checkmarkSprite->setPosition(sf::Vector2f(
                oldPos.x + posChange.x,
                oldPos.y + posChange.y
            ));
        }
    }
};
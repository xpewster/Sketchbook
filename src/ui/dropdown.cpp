#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

class DropdownSelector {
public:
    sf::RectangleShape box;
    sf::Text displayText;
    std::vector<std::string> options;
    int selectedIndex = 0;
    bool expanded = false;
    int hoveredIndex = -1;
    
    sf::Font& font;
    float itemHeight;
    sf::Vector2f position;
    sf::Vector2f size;
    
    DropdownSelector(float x, float y, float w, float h, 
                     const std::vector<std::string>& opts, sf::Font& f, int defaultIndex = 0)
        : options(opts), selectedIndex(defaultIndex), font(f), 
          itemHeight(h), position(x, y), size(w, h), displayText(f, "", 14) {
        
        box.setPosition(sf::Vector2f(x, y));
        box.setSize(sf::Vector2f(w, h));
        box.setFillColor(sf::Color::White);
        box.setOutlineColor(sf::Color(100, 100, 100));
        box.setOutlineThickness(1);
        
        displayText.setFillColor(sf::Color::Black);
        displayText.setPosition(sf::Vector2f(x + 4, y + 4));
        if (!options.empty()) {
            displayText.setString(options[selectedIndex]);
        }
    }
    
    void handleEvent(const sf::Event& event, sf::Vector2i mousePos, sf::RenderWindow& window) {
        const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
        
        if (const auto* buttonPressed = event.getIf<sf::Event::MouseButtonPressed>()) {
            if (expanded) {
                // Check if clicking on an option
                bool clickedOption = false;
                for (size_t i = 0; i < options.size(); ++i) {
                    sf::FloatRect optionBounds(
                        sf::Vector2f(position.x, position.y + itemHeight * (i + 1)),
                        sf::Vector2f(size.x, itemHeight)
                    );
                    
                    if (optionBounds.contains(mousePosF)) {
                        selectedIndex = i;
                        displayText.setString(options[selectedIndex]);
                        clickedOption = true;
                        break;
                    }
                }
                expanded = false;
            } else {
                // Check if clicking on the main box
                if (box.getGlobalBounds().contains(mousePosF)) {
                    expanded = true;
                }
            }
        }
        
        // Track hover state when expanded
        if (expanded) {
            if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
                hoveredIndex = -1;
                for (size_t i = 0; i < options.size(); ++i) {
                    sf::FloatRect optionBounds(
                        sf::Vector2f(position.x, position.y + itemHeight * (i + 1)),
                        sf::Vector2f(size.x, itemHeight)
                    );
                    
                    if (optionBounds.contains(mousePosF)) {
                        hoveredIndex = i;
                        break;
                    }
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
        // Draw main box
        window.draw(box);
        window.draw(displayText);
        
        // Draw dropdown arrow
        sf::ConvexShape arrow(3);
        float arrowSize = 6;
        float arrowX = position.x + size.x - 16;
        float arrowY = position.y + itemHeight / 2;
        
        if (!expanded) {
            // Down arrow
            arrow.setPoint(0, sf::Vector2f(arrowX, arrowY - 2));
            arrow.setPoint(1, sf::Vector2f(arrowX + arrowSize, arrowY - 2));
            arrow.setPoint(2, sf::Vector2f(arrowX + arrowSize / 2, arrowY + 4));
        } else {
            // Up arrow
            arrow.setPoint(0, sf::Vector2f(arrowX, arrowY + 2));
            arrow.setPoint(1, sf::Vector2f(arrowX + arrowSize, arrowY + 2));
            arrow.setPoint(2, sf::Vector2f(arrowX + arrowSize / 2, arrowY - 4));
        }
        arrow.setFillColor(sf::Color(100, 100, 100));
        window.draw(arrow);
        
        // Draw dropdown options if expanded
        if (expanded) {
            for (size_t i = 0; i < options.size(); ++i) {
                sf::RectangleShape optionBox(sf::Vector2f(size.x, itemHeight));
                optionBox.setPosition(sf::Vector2f(position.x, position.y + itemHeight * (i + 1)));
                
                // Highlight hovered or selected item
                if ((int)i == hoveredIndex) {
                    optionBox.setFillColor(sf::Color(230, 230, 230));
                } else if ((int)i == selectedIndex) {
                    optionBox.setFillColor(sf::Color(240, 240, 255));
                } else {
                    optionBox.setFillColor(sf::Color::White);
                }
                
                optionBox.setOutlineColor(sf::Color(100, 100, 100));
                optionBox.setOutlineThickness(1);
                window.draw(optionBox);
                
                sf::Text optionText(font, options[i], 14);
                optionText.setFillColor(sf::Color::Black);
                optionText.setPosition(sf::Vector2f(position.x + 4, position.y + itemHeight * (i + 1) + 4));
                window.draw(optionText);
            }
        }
    }
    
    int getSelectedIndex() const { return selectedIndex; }
    std::string getSelectedValue() const { 
        return options.empty() ? "" : options[selectedIndex]; 
    }
    
    void setSelectedIndex(int index) {
        if (index >= 0 && index < (int)options.size()) {
            selectedIndex = index;
            displayText.setString(options[selectedIndex]);
        }
    }
};
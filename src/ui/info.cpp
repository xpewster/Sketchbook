#include <SFML/Graphics.hpp>
#include <string>
#include <optional>
#include <vector>

#include "button.cpp"
#include "text_box.cpp"

enum class InfoBoxDirection {
    Up, // Not yet supported
    Down,
    Left,
    Right // Not yet supported
};

class InfoIcon {
public:
    sf::Texture iconTex;
    std::optional<sf::Sprite> iconSprite;
    bool hovered = false;
    
    sf::Vector2f position;
    float iconSize;
    
    std::string infoText;
    sf::Font& font;
    
    // Info box properties
    sf::RectangleShape infoBox;
    InfoBoxDirection boxDirection;
    sf::Text infoTextDisplay;
    sf::ConvexShape tail;
    sf::RectangleShape tailCover; // To cover tail overlap with box outline
    float boxWidth = 200;
    float boxPadding = 8;
    float extraHeight = 0; // Extra height added to box for padding and action elements
    bool hoverOverBoxCounts = false; // Whether hovering over the info box itself should keep it open
    
    InfoIcon(float x, float y, float size, const std::string& iconPath, 
             const std::string& info, sf::Font& f, InfoBoxDirection direction = InfoBoxDirection::Down)
        : position(x, y), iconSize(size), infoText(info), font(f),
          infoTextDisplay(f, info, 12), boxDirection(direction) {
        
        // Load icon
        if (iconTex.loadFromFile(iconPath)) {
            iconTex.setSmooth(true);
            iconSprite.emplace(iconTex);
            sf::Vector2u texSize = iconTex.getSize();
            float scale = size/texSize.x;
            iconSprite->setScale(sf::Vector2f(scale, scale));
            iconSprite->setPosition(sf::Vector2f(x, y));
        }
        
        // Setup info text
        infoTextDisplay.setFillColor(sf::Color::Black);
        infoTextDisplay.setLineSpacing(1.2f);
        
        // Calculate text wrapping and box size
        wrapText();
        
        // Setup tail (triangle pointing up)
        tail = sf::ConvexShape(3);
        tail.setFillColor(sf::Color::White);
        tail.setOutlineColor(sf::Color(100, 100, 100));
        tail.setOutlineThickness(1);

        // Setup tail cover to hide outline overlap
        tailCover.setFillColor(sf::Color::White);
        tailCover.setSize(sf::Vector2f(12, 4));
    }

    void setExtraHeight(float height) {
        extraHeight = height;
    }
    
    void enableHoverOverBox(bool enable) {
        hoverOverBoxCounts = enable;
    }

    void setInfoText(const std::string& info) {
        infoText = info;
        infoTextDisplay.setString(info);
        wrapText();
    }
    
    void setBoxWidth(float width) {
        boxWidth = width;
        wrapText();
    }
    
    void handleEvent(const sf::Event& event, sf::Vector2i mousePos, sf::RenderWindow& window) {
        const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
        
        if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
            bool overIcon = iconSprite && iconSprite->getGlobalBounds().contains(mousePosF);
            bool overBox = hoverOverBoxCounts && hovered && (infoBox.getGlobalBounds().contains(mousePosF) || tail.getGlobalBounds().contains(mousePosF));
            hovered = overIcon || overBox;
        }
    }
    
    void draw(sf::RenderWindow& window) {
        if (iconSprite) {
            window.draw(*iconSprite);
        }
        
        if (hovered) {
            // Position info box below icon
            float boxX = 0.0f;
            float boxY = 0.0f;

            if (boxDirection == InfoBoxDirection::Down) {
                boxX = position.x + iconSize / 2 - boxWidth / 2;
                boxY = position.y + iconSize + 10; // 10px gap + tail height
            } else if (boxDirection == InfoBoxDirection::Left) {
                boxX = position.x + iconSize / 2 + 30 - boxWidth; // 30px margin
                boxY = position.y + iconSize + 10;
            }
            
            sf::FloatRect textBounds = infoTextDisplay.getLocalBounds();
            float boxHeight = textBounds.size.y + boxPadding * 2;
            
            // Setup box
            infoBox.setPosition(sf::Vector2f(boxX, boxY));
            infoBox.setSize(sf::Vector2f(boxWidth, boxHeight + extraHeight));
            infoBox.setFillColor(sf::Color::White);
            infoBox.setOutlineColor(sf::Color(100, 100, 100));
            infoBox.setOutlineThickness(1);
            
            // Setup tail pointing up to icon
            float tailHeight = 8;
            float tailWidth = 12;
            float tailX = position.x + iconSize / 2;
            float tailY = position.y + iconSize + 2;
            
            tail.setPoint(0, sf::Vector2f(tailX, tailY)); // Top point
            tail.setPoint(1, sf::Vector2f(tailX - tailWidth / 2, tailY + tailHeight)); // Bottom left
            tail.setPoint(2, sf::Vector2f(tailX + tailWidth / 2, tailY + tailHeight)); // Bottom right

            // Position tail cover to hide outline overlap
            tailCover.setPosition(sf::Vector2f(tailX - tailWidth / 2, tailY + tailHeight - 1));
            
            // Position text
            infoTextDisplay.setPosition(sf::Vector2f(
                boxX + boxPadding,
                boxY + boxPadding
            ));
            
            // Draw in order: tail, box, text (so outline overlaps properly)
            window.draw(tail);
            window.draw(infoBox);
            window.draw(tailCover);
            window.draw(infoTextDisplay);
        }
    }

    bool isHovered() const {
        return hovered;
    }

private:
    void wrapText() {
        // Simple word wrapping
        std::vector<std::string> words;
        std::string word;
        for (char c : infoText) {
            if (c == ' ' || c == '\n') {
                if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
                if (c == '\n') {
                    words.push_back("\n");
                }
            } else {
                word += c;
            }
        }
        if (!word.empty()) {
            words.push_back(word);
        }
        
        std::string wrappedText;
        std::string currentLine;
        float maxWidth = boxWidth - boxPadding * 2;
        
        for (const auto& w : words) {
            if (w == "\n") {
                wrappedText += currentLine + "\n";
                currentLine.clear();
                continue;
            }
            
            std::string testLine = currentLine.empty() ? w : currentLine + " " + w;
            infoTextDisplay.setString(testLine);
            
            if (infoTextDisplay.getLocalBounds().size.x > maxWidth && !currentLine.empty()) {
                wrappedText += currentLine + "\n";
                currentLine = w;
            } else {
                currentLine = testLine;
            }
        }
        
        if (!currentLine.empty()) {
            wrappedText += currentLine;
        }
        
        infoTextDisplay.setString(wrappedText);
    }
};
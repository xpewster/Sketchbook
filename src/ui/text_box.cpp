#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <algorithm>
#include <optional>

class TextInput {
public:
    sf::RectangleShape box;
    sf::Text text;
    std::string value;
    bool focused = false;
    size_t cursorPos = 0;
    sf::Clock cursorBlinkClock;

    float boxX, boxY;
    float width, height;
    float padding = 4.f;
    float scrollOffset = 0.f;

    std::optional<size_t> selectionAnchor;
    bool dragging = false;

    TextInput(float x, float y, float w, float h, const std::string& initial, sf::Font& font,
              bool cursorAtEnd = true)
        : text(font, initial, 14), value(initial),
          cursorPos(cursorAtEnd ? initial.size() : 0),
          boxX(x), boxY(y), width(w), height(h) {
        box.setPosition(sf::Vector2f(x, y));
        box.setSize(sf::Vector2f(w, h));
        box.setFillColor(sf::Color::White);
        box.setOutlineColor(sf::Color(100, 100, 100));
        box.setOutlineThickness(1);

        text.setFillColor(sf::Color::Black);
        text.setPosition(sf::Vector2f(x + padding, y + padding));

        ensureCursorVisible();
    }

    bool hasSelection() const { return selectionAnchor.has_value() && *selectionAnchor != cursorPos; }

    std::pair<size_t, size_t> getSelectionRange() const {
        size_t a = selectionAnchor.value_or(cursorPos);
        return { min(a, cursorPos), max(a, cursorPos) };
    }

    std::string getSelectedText() const {
        auto [start, end] = getSelectionRange();
        return value.substr(start, end - start);
    }

    bool isFocused() const { return focused; }

    void clearSelection() { selectionAnchor.reset(); }

    void deleteSelection() {
        if (!hasSelection()) return;
        auto [start, end] = getSelectionRange();
        value.erase(start, end - start);
        cursorPos = start;
        clearSelection();
        text.setString(value);
    }

    // Hit-test a world-space X (already scroll-adjusted) against character positions
    size_t cursorFromX(float adjustedX) const {
        size_t pos = 0;
        for (size_t i = 0; i <= value.size(); ++i) {
            float charX = text.findCharacterPos(i).x;
            if (adjustedX < charX - padding) break;
            pos = i;
        }
        return pos;
    }

    // Keep cursor within the visible content area
    void ensureCursorVisible() {
        float cursorWorldX = text.findCharacterPos(cursorPos).x;
        float textStartX = text.getPosition().x;
        float cursorRelX = cursorWorldX - textStartX;
        float contentWidth = width - padding;

        if (cursorRelX - scrollOffset > contentWidth) {
            scrollOffset = cursorRelX - contentWidth;
        }
        if (cursorRelX - scrollOffset < 0.f) {
            scrollOffset = cursorRelX;
        }
        if (scrollOffset < 0.f) scrollOffset = 0.f;
    }

    void handleEvent(const sf::Event& event, sf::Vector2i mousePos, sf::RenderWindow& window) {
        if (const auto* buttonPressed = event.getIf<sf::Event::MouseButtonPressed>()) {
            const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
            focused = box.getGlobalBounds().contains(mousePosF);
            box.setOutlineColor(focused ? sf::Color::Blue : sf::Color(100, 100, 100));

            if (focused) {
                // Find cursor position based on click (adjusted for scroll)
                float adjustedMouseX = mousePosF.x + scrollOffset;
                cursorPos = cursorFromX(adjustedMouseX);
                selectionAnchor = cursorPos;
                dragging = true;
                cursorBlinkClock.restart();
                ensureCursorVisible();
            } else {
                clearSelection();
                dragging = false;
            }
        }

        if (const auto* buttonReleased = event.getIf<sf::Event::MouseButtonReleased>()) {
            if (dragging) {
                dragging = false;
                // Plain click with no drag — clear selection
                if (selectionAnchor.has_value() && *selectionAnchor == cursorPos) {
                    clearSelection();
                }
            }
        }

        // Drag to extend selection
        if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
            if (dragging) {
                const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
                float adjustedMouseX = mousePosF.x + scrollOffset;
                cursorPos = cursorFromX(adjustedMouseX);
                cursorBlinkClock.restart();
                ensureCursorVisible();
            }
        }

        if (focused) {
            if (const auto* textEntered = event.getIf<sf::Event::TextEntered>()) {
                if (textEntered->unicode == '\b') {
                    // Backspace: delete selection or char before cursor
                    if (hasSelection()) {
                        deleteSelection();
                    } else if (cursorPos > 0) {
                        value.erase(cursorPos - 1, 1);
                        cursorPos--;
                    }
                    text.setString(value);
                } else if (textEntered->unicode >= 32 && textEntered->unicode < 127) {
                    // Replace selection if active, then insert at cursor position
                    if (hasSelection()) deleteSelection();
                    value.insert(cursorPos, 1, static_cast<char>(textEntered->unicode));
                    cursorPos++;
                    text.setString(value);
                }
                cursorBlinkClock.restart();
                ensureCursorVisible();
            }

            // Handle arrow keys and keyboard shortcuts
            if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
                bool ctrl = keyPressed->control;

                if (ctrl && keyPressed->code == sf::Keyboard::Key::A) {
                    // Select all
                    selectionAnchor = 0;
                    cursorPos = value.size();
                    cursorBlinkClock.restart();
                    ensureCursorVisible();
                } else if (ctrl && keyPressed->code == sf::Keyboard::Key::C) {
                    // Copy selection to clipboard
                    if (hasSelection()) {
                        sf::Clipboard::setString(getSelectedText());
                    }
                } else if (ctrl && keyPressed->code == sf::Keyboard::Key::X) {
                    // Cut selection to clipboard
                    if (hasSelection()) {
                        sf::Clipboard::setString(getSelectedText());
                        deleteSelection();
                        cursorBlinkClock.restart();
                        ensureCursorVisible();
                    }
                } else if (ctrl && keyPressed->code == sf::Keyboard::Key::V) {
                    // Paste from clipboard
                    std::string clip = sf::Clipboard::getString();
                    if (!clip.empty()) {
                        if (hasSelection()) deleteSelection();
                        value.insert(cursorPos, clip);
                        cursorPos += clip.size();
                        text.setString(value);
                        cursorBlinkClock.restart();
                        ensureCursorVisible();
                    }
                } else if (keyPressed->code == sf::Keyboard::Key::Left && cursorPos > 0) {
                    clearSelection();
                    cursorPos--;
                    cursorBlinkClock.restart();
                    ensureCursorVisible();
                } else if (keyPressed->code == sf::Keyboard::Key::Right && cursorPos < value.size()) {
                    clearSelection();
                    cursorPos++;
                    cursorBlinkClock.restart();
                    ensureCursorVisible();
                }
            }
        }
    }

    bool update(sf::Vector2i mousePos, sf::RenderWindow& window) {
        // Auto-scroll while drag-selecting past box edges
        if (dragging) {
            const sf::Vector2f mousePosF = window.mapPixelToCoords(mousePos);
            float scrollSpeed = 3.f;
            if (mousePosF.x < boxX) {
                scrollOffset = max(0.f, scrollOffset - scrollSpeed);
            } else if (mousePosF.x > boxX + width) {
                scrollOffset += scrollSpeed;
            }
            // Re-resolve cursor position with updated scroll
            float adjustedMouseX = mousePosF.x + scrollOffset;
            cursorPos = cursorFromX(adjustedMouseX);
            ensureCursorVisible();
            return true;
        }
        if (focused) return true;
        bool hover = box.getGlobalBounds().contains(window.mapPixelToCoords(mousePos));
        box.setOutlineColor(hover ? sf::Color(135, 135, 135) : sf::Color(100, 100, 100));
        return hover;
    }

    void draw(sf::RenderWindow& window) {
        window.draw(box);

        // Clip text to box via sf::View (viewport matches box outline)
        sf::View oldView = window.getView();
        sf::Vector2u winSize = window.getSize();

        sf::View clipView;
        clipView.setViewport(sf::FloatRect(
            sf::Vector2f(boxX / winSize.x, boxY / winSize.y),
            sf::Vector2f(width / winSize.x, height / winSize.y)
        ));

        // Shift view by scrollOffset to scroll text
        clipView.setCenter(sf::Vector2f(
            boxX + scrollOffset + width / 2.f,
            boxY + height / 2.f
        ));
        clipView.setSize(sf::Vector2f(width, height));

        window.setView(clipView);

        // Draw selection highlight behind text
        if (hasSelection()) {
            auto [start, end] = getSelectionRange();
            float selStartX = text.findCharacterPos(start).x;
            float selEndX = text.findCharacterPos(end).x;
            sf::RectangleShape selRect(sf::Vector2f(selEndX - selStartX, static_cast<float>(text.getCharacterSize()) + 2));
            selRect.setPosition(sf::Vector2f(selStartX, text.getPosition().y));
            selRect.setFillColor(sf::Color(100, 150, 255, 100));
            window.draw(selRect);
        }

        window.draw(text);

        // Draw blinking cursor when focused
        if (focused && (int)(cursorBlinkClock.getElapsedTime().asSeconds() * 2) % 2 == 0) {
            float cursorWorldX = text.findCharacterPos(cursorPos).x;
            sf::RectangleShape cursor(sf::Vector2f(1.f, static_cast<float>(text.getCharacterSize())));
            cursor.setPosition(sf::Vector2f(cursorWorldX, text.getPosition().y + 1));
            cursor.setFillColor(sf::Color::Black);
            window.draw(cursor);
        }

        // Restore the original view
        window.setView(oldView);
    }
};
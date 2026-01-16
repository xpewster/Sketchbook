
#include <SFML/Graphics.hpp>
#include "../image.hpp"

// Convert RenderTexture to RGB565 for Qualia
void textureToRGB565(sf::RenderTexture& texture, qualia::Image& image) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(x, y));
            image.at(x, y) = qualia::rgb565(c.r, c.g, c.b);
        }
    }
}

// Rotate 90 degrees clockwise during conversion
void textureToRGB565Rot90(sf::RenderTexture& texture, qualia::Image& image) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    // texture is (height, width), output is (width, height)
    // Output pixel (x, y) comes from input pixel (y, width-1-x)
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(y, image.width - 1 - x));
            image.at(x, y) = qualia::rgb565(c.r, c.g, c.b);
        }
    }
}

// Rotate 90 degrees counter-clockwise during conversion  
void textureToRGB565RotNeg90(sf::RenderTexture& texture, qualia::Image& image) {
    sf::Image sfImg = texture.getTexture().copyToImage();
    
    // Output pixel (x, y) comes from input pixel (height-1-y, x)
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            sf::Color c = sfImg.getPixel(sf::Vector2u(image.height - 1 - y, x));
            image.at(x, y) = qualia::rgb565(c.r, c.g, c.b);
        }
    }
}

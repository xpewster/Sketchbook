#include <SFML/Graphics.hpp>
#include <string>

#include "../system_stats.h"

class Skin {
private:
    std::string name;

public:

    void initialize(const std::string& skinName, const std::string& xmlFilePath);

    void draw(sf::RenderTexture& texture, SystemStats& stats);

    virtual ~Skin() = default;
};

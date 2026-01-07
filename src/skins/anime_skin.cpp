#include "skin.h"

/*

 [AnimeSkin] - A skin with an animated background and character.

 Parameters:
    - "skin.background.png": Path to the background image. For animation there will be multiple frames named "skin.background.0.png", "skin.background.1.png", etc.
    - "skin.background.animation": Whether to enable animated background (true/false)
    - "skin.background.animation.speed": Speed of the background animation (frames per second)
    - "skin.background.framecount": Number of frames in the background animation
    - "skin.character.animation": Whether to enable character animation (true/false)
    - "skin.character.animation.speed": Speed of the character animation (frames per second)
    - "skin.character.framecount": Number of frames in the character animation
    - "skin.character.flip": Whether to flip the character horizontally (true/false)
    - "skin.character.png": Path to the character image
    - "skin.character.x": X position of the character
    - "skin.character.y": Y position of the character
    - "skin.character.bobbing": Whether to enable bobbing animation for the character (true/false)
    - "skin.character.bobbing.speed": Speed of the bobbing animation
    - "skin.character.hot.png": Path to the character image when CPU is hot
    - "skin.weather.icon.sunny.png": Path to sunny weather icon
    - "skin.weather.icon.rainy.png": Path to rainy weather icon
    - "skin.weather.icon.cloudy.png": Path to cloudy weather icon
    - "skin.weather.icon.night.png": Path to night weather icon
    - "skin.weather.icon.windy.png": Path to windy weather icon
    - "skin.weather.icon.width": Width of weather icon
    - "skin.weather.icon.height": Height of weather icon
    - "skin.weather.icon.x": X position of weather icon
    - "skin.weather.icon.y": Y position of weather icon
    - "skin.weather.icon.bobbing": Whether to enable bobbing animation for weather icon (true/false)
    - "skin.weather.icon.bobbing.speed": Speed of bobbing animation
    - "skin.weather.text.fontindex": Index of font to use for weather text
    - "skin.weather.text.x": X position of weather text
    - "skin.weather.text.y": Y position of weather text
    - "skin.weather.text.color": Color of weather text (hex RGB)
    - "skin.weather.text.size": Font size of weather text
    - "skin.hwmon.text.fontindex": Index of font to use for CPU text
    - "skin.hwmon.cpu.usage.text.x": X position of CPU usage text
    - "skin.hwmon.cpu.usage.text.y": Y position of CPU usage text
    - "skin.hwmon.cpu.usage.text.color": Color of CPU usage text (hex RGB)
    - "skin.hwmon.cpu.usage.text.size": Font size of CPU usage text
    - "skin.hwmon.cpu.usage.icon.path": Path to CPU usage icon
    - "skin.hwmon.cpu.temp.text.x": X position of CPU temperature text
    - "skin.hwmon.cpu.temp.text.y": Y position of CPU temperature text
    - "skin.hwmon.cpu.temp.text.color": Color of CPU temperature text (hex RGB)
    - "skin.hwmon.cpu.temp.text.size": Font size of CPU temperature text
    - "skin.hwmon.cpu.temp.icon.path": Path to CPU temperature icon
    - "skin.hwmon.cpu.combine": Whether to combine CPU usage and temperature into one line
    - "skin.hwmon.mem.usage.text.x": X position of memory usage text
    - "skin.hwmon.mem.usage.text.y": Y position of memory usage text
    - "skin.hwmon.mem.usage.text.color": Color of memory usage text (hex RGB)
    - "skin.hwmon.mem.usage.text.size": Font size of memory usage text
    - "skin.hwmon.mem.usage.icon.path": Path to memory usage icon
*/

class AnimeSkin : public Skin {
public:
    AnimeSkin(std::string name, int width, int height) : Skin(name, width, height) {}

    void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather) override {
        texture.clear(sf::Color::Black);

        frameCount++;

    }
};
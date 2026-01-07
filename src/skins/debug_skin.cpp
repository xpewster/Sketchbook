#include "skin.h"

class DebugSkin : public Skin {
public:
    DebugSkin(std::string name, int width, int height) : Skin(name, width, height) {}

    void draw(sf::RenderTexture& texture, SystemStats& stats, WeatherData& weather) override {
        texture.clear(sf::Color::Black);
        
        int y = 20;
        
        // Title
        sf::Text title(font, "SYSTEM MONITOR", 28);
        title.setPosition(sf::Vector2f(20, (float)y));
        title.setFillColor(sf::Color(100, 200, 255));
        texture.draw(title);
        y += 50;
        
        // CPU section
        sf::Text cpuLabel(font, "CPU", 18);
        cpuLabel.setPosition(sf::Vector2f(20, (float)y));
        cpuLabel.setFillColor(sf::Color::White);
        texture.draw(cpuLabel);
        y += 30;
        
        // CPU bar background
        sf::RectangleShape cpuBarBg(sf::Vector2f((float)(DISPLAY_WIDTH - 40), 25));
        cpuBarBg.setPosition(sf::Vector2f(20, (float)y));
        cpuBarBg.setFillColor(sf::Color(0, 100, 0));
        texture.draw(cpuBarBg);
        
        // CPU bar fill
        float cpuBarWidth = (DISPLAY_WIDTH - 40) * stats.cpuPercent / 100.0f;
        sf::RectangleShape cpuBarFill(sf::Vector2f(cpuBarWidth, 25));
        cpuBarFill.setPosition(sf::Vector2f(20, (float)y));
        cpuBarFill.setFillColor(sf::Color::Green);
        texture.draw(cpuBarFill);
        
        // CPU percentage text
        char cpuText[32];
        snprintf(cpuText, sizeof(cpuText), "%.1f%%", stats.cpuPercent);
        sf::Text cpuPct(font, cpuText, 18);
        cpuPct.setPosition(sf::Vector2f((float)(DISPLAY_WIDTH - 70), (float)(y + 2)));
        cpuPct.setFillColor(sf::Color::White);
        texture.draw(cpuPct);

        // CPU temperature text
        char tempText[32];
        snprintf(tempText, sizeof(tempText), "Temp: %.1f C", stats.cpuTempC);
        sf::Text tempLabel(font, tempText, 14);
        tempLabel.setPosition(sf::Vector2f(20, (float)(y + 30)));
        tempLabel.setFillColor(sf::Color(150, 150, 150));
        texture.draw(tempLabel);
        y += 45;
        
        // Memory section
        sf::Text memLabel(font, "MEMORY", 18);
        memLabel.setPosition(sf::Vector2f(20, (float)y));
        memLabel.setFillColor(sf::Color::White);
        texture.draw(memLabel);
        y += 30;
        
        // Memory bar background
        sf::RectangleShape memBarBg(sf::Vector2f((float)(DISPLAY_WIDTH - 40), 25));
        memBarBg.setPosition(sf::Vector2f(20, (float)y));
        memBarBg.setFillColor(sf::Color(0, 0, 100));
        texture.draw(memBarBg);
        
        // Memory bar fill
        float memBarWidth = (DISPLAY_WIDTH - 40) * stats.memPercent / 100.0f;
        sf::RectangleShape memBarFill(sf::Vector2f(memBarWidth, 25));
        memBarFill.setPosition(sf::Vector2f(20, (float)y));
        memBarFill.setFillColor(sf::Color::Blue);
        texture.draw(memBarFill);
        
        // Memory percentage text
        char memText[32];
        snprintf(memText, sizeof(memText), "%.1f%%", stats.memPercent);
        sf::Text memPct(font, memText, 18);
        memPct.setPosition(sf::Vector2f((float)(DISPLAY_WIDTH - 70), (float)(y + 2)));
        memPct.setFillColor(sf::Color::White);
        texture.draw(memPct);
        y += 35;
        
        // Memory usage text
        char memUsage[64];
        snprintf(memUsage, sizeof(memUsage), "%llu / %llu MB",
                (unsigned long long)stats.memUsedMB,
                (unsigned long long)stats.memTotalMB);
        sf::Text memUsageText(font, memUsage, 14);
        memUsageText.setPosition(sf::Vector2f(20, (float)y));
        memUsageText.setFillColor(sf::Color(150, 150, 150));
        texture.draw(memUsageText);

        // Weather section
        y += 50;
        sf::Text weatherLabel(font, "WEATHER", 18);
        weatherLabel.setPosition(sf::Vector2f(20, (float)y));
        weatherLabel.setFillColor(sf::Color::White);
        texture.draw(weatherLabel);
        y += 30;

        // Weather icon code 
        char weatherText[64];
        snprintf(weatherText, sizeof(weatherText), "Icon: %s, Temp: %.1f %s, Wind: %.1f mph",
                weather.iconCode.c_str(), weather.currentTemp, "F", weather.windSpeed);
        sf::Text weatherInfo(font, weatherText, 14);
        weatherInfo.setPosition(sf::Vector2f(20, (float)y));
        weatherInfo.setFillColor(sf::Color(150, 150, 150));
        texture.draw(weatherInfo);
        
        texture.display();
    }
};

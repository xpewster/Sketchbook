#pragma once

#include "../http.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <iostream>

using json = nlohmann::json;

struct WeatherData {
    float currentTemp = 0;
    float todayMinTemp = 0;
    float todayMaxTemp = 0;
    float tomorrowMinTemp = 0;
    float tomorrowMaxTemp = 0;
    float windSpeed = 0;
    std::string currentDescription;
    std::string todayDescription;
    std::string tomorrowDescription;
    std::string iconCode;  // e.g., "04n"
    bool isNight = false;  // true if icon ends with 'n'
    int sunrise = 0;  // unix timestamp
    int sunset = 0;   // unix timestamp
    bool available = false; // true if data is valid
};

class WeatherMonitor {
public:
    WeatherMonitor(const std::string& apiKey, float lat, float lon, const std::string& units = "imperial")
        : apiKey_(apiKey), lat_(lat), lon_(lon), units_(units) {
        lastUpdateTime_ = std::chrono::steady_clock::now() - std::chrono::hours(1); // Force initial update
        std::cout << "WeatherMonitor initialized with API key: " << apiKey_ 
                  << ", lat: " << lat_ << ", lon: " << lon_ 
                  << ", units: " << units_ << "\n";
    }
    
    WeatherData getWeather() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastUpdateTime_).count();
        
        // Update every 10 minutes (OpenWeatherMap updates ~every 10 min)
        if (elapsed >= 10) {
            fetchWeather();
            lastUpdateTime_ = now;
        }
        
        return cachedWeather_;
    }

private:
    void fetchWeather() {
        // Build URL
        std::string path = "/data/3.0/onecall?lat=" + std::to_string(lat_) +
                          "&lon=" + std::to_string(lon_) +
                          "&units=" + units_ +
                          "&appid=" + apiKey_;
        
        HttpResponse response = get("http://api.openweathermap.org" + path);
        
        if (response.isOk()) {
            parseWeatherData(response.body);
            std::cout << "Weather data updated successfully: [IconCode " << cachedWeather_.iconCode << "] [IsNight " << cachedWeather_.isNight << "] [Temp " << cachedWeather_.currentTemp << "] [WindSpeed " << cachedWeather_.windSpeed << "]\n";
            cachedWeather_.available = true;
        } else {
            std::cerr << "Weather API request failed: " << response.statusCode << "\n";
            cachedWeather_.available = false;
        }
    }
    
    void parseWeatherData(const std::string& jsonStr) {
        try {
            json data = json::parse(jsonStr);
            
            // Current weather
            if (data.contains("current")) {
                auto current = data["current"];
                cachedWeather_.currentTemp = current["temp"].get<float>();
                cachedWeather_.windSpeed = current["wind_speed"].get<float>();
                cachedWeather_.sunrise = current["sunrise"].get<int>();
                cachedWeather_.sunset = current["sunset"].get<int>();
                
                if (current.contains("weather") && !current["weather"].empty()) {
                    auto weather = current["weather"][0];
                    cachedWeather_.currentDescription = weather["description"].get<std::string>();
                    cachedWeather_.iconCode = weather["icon"].get<std::string>();
                    
                    // Check if night (icon ends with 'n')
                    if (!cachedWeather_.iconCode.empty()) {
                        cachedWeather_.isNight = (cachedWeather_.iconCode.back() == 'n');
                    }
                }
            }
            
            // Daily forecast
            if (data.contains("daily") && data["daily"].size() >= 2) {
                // Today (index 0)
                auto today = data["daily"][0];
                cachedWeather_.todayMinTemp = today["temp"]["min"].get<float>();
                cachedWeather_.todayMaxTemp = today["temp"]["max"].get<float>();
                if (today.contains("weather") && !today["weather"].empty()) {
                    cachedWeather_.todayDescription = today["weather"][0]["description"].get<std::string>();
                }
                
                // Tomorrow (index 1)
                auto tomorrow = data["daily"][1];
                cachedWeather_.tomorrowMinTemp = tomorrow["temp"]["min"].get<float>();
                cachedWeather_.tomorrowMaxTemp = tomorrow["temp"]["max"].get<float>();
                if (tomorrow.contains("weather") && !tomorrow["weather"].empty()) {
                    cachedWeather_.tomorrowDescription = tomorrow["weather"][0]["description"].get<std::string>();
                }
            }
            
        } catch (const json::exception& e) {
            std::cerr << "JSON parsing error: " << e.what() << "\n";
        }
    }

    std::string apiKey_;
    float lat_;
    float lon_;
    std::string units_;
    WeatherData cachedWeather_;
    std::chrono::steady_clock::time_point lastUpdateTime_;
};
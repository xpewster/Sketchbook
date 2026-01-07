#pragma once

#include "../weather.hpp"

// Helper function to determine if CPU is "hot" based on temperature
inline bool isCpuHot(float tempC) {
    return tempC >= 60.0f; // Threshold for "hot" CPU
}

// Helper function to determine the icon to use based on weather conditions
inline std::string getWeatherIconName(const WeatherData& weather) {
    auto iconCode = weather.iconCode.substr(0, 2);
    std::string iconName;
    if (iconCode == "01") iconName += "clear";
    else if (iconCode == "02") iconName += "partlycloudy";
    else if (iconCode == "03") iconName += "mostlycloudy";
    else if (iconCode == "04") iconName += "cloudy";
    else if (iconCode == "09") iconName += "showers";
    else if (iconCode == "10") iconName += "rain";
    else if (iconCode == "11") iconName += "thunderstorm";
    else if (iconCode == "13") iconName += "snow";
    else if (iconCode == "50") iconName += "fog";
    iconName += weather.isNight ? "_n" : "_d";
    if (weather.windSpeed >= 15) {
        iconName += "@windy";
    }
    return iconName;
}

// Helper function to determine the icon to use based on weather conditions with less granularity
inline std::string getWeatherIconNameSimplified(const WeatherData& weather) {
    auto iconCode = weather.iconCode.substr(0, 2);
    if (weather.windSpeed >= 15) {
        return "windy";
    } else {
        if (!weather.isNight) {
            if (iconCode == "01" || iconCode == "02") return "sunny";
            else if (iconCode == "03" || iconCode == "04") return "cloudy";
            else if (iconCode == "09" || iconCode == "10") return "rainy";
            else if (iconCode == "11") return "thunderstorm";
            else if (iconCode == "13") return "sunny"; // Treat snow as sunny for now
            else if (iconCode == "50") return "foggy";
        } else {
            if (iconCode == "01" || iconCode == "02" || iconCode == "03") return "night";
            else if (iconCode == "04") return "cloudy";
            else if (iconCode == "09" || iconCode == "10") return "rainy";
            else if (iconCode == "11") return "thunderstorm";
            else if (iconCode == "13") return "night"; // Treat snow as sunny for now
            else if (iconCode == "50") return "foggy";
        }
    }
}
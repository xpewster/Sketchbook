#pragma once
#include <toml++/toml.h>
#include <windows.h>
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>

class Settings {
public:
    // Weather settings
    struct WeatherConfig {
        std::string apiKey;
        float latitude = 47.6062f;  // Default: Seattle
        float longitude = -122.3321f;
        std::string units = "imperial";  // imperial, metric, standard
    };
    
    // Display settings
    // struct DisplayConfig {
    //     int updateInterval = 1000;  // ms
    //     int windowWidth = 800;
    //     int windowHeight = 600;
    //     bool fullscreen = false;
    // };
    
    // Network settings
    struct NetworkConfig {
        std::string espIP = "192.168.1.100";
        int espPort = 8080;
    };

    struct Preferences {
        std::string selectedSkin = "Debug";
        bool rotate180 = false;
    };

    struct TrainConfig {
        std::string apiKey;
        std::string stopId0;
        std::string stopId1;
        std::string apiBase;
    };
    
    WeatherConfig weather;
    // DisplayConfig display;
    NetworkConfig network;
    Preferences preferences;
    TrainConfig train;
    
    // Load settings from settings.toml in exe directory
    bool load() {
        std::filesystem::path settingsPath = getExeDirectory() / "settings.toml";
        
        // Create default settings file if it doesn't exist
        if (!std::filesystem::exists(settingsPath)) {
            std::cout << "settings.toml not found, creating default...\n";
            createDefaultSettings(settingsPath);
        }

        std::cout << "Loading settings from: " << settingsPath << "\n";
        
        try {
            auto config = toml::parse_file(settingsPath.string());
            std::cout << "Parsed settings.toml successfully\n";

            if (auto prefTable = config["preferences"].as_table()) {
                preferences.selectedSkin = (*prefTable)["selected_skin"].value_or("Debug");
                preferences.rotate180 = (*prefTable)["rotate_180"].value_or(false);
            }
            
            // Parse weather settings
            if (auto weatherTable = config["weather"].as_table()) {
                weather.apiKey = (*weatherTable)["OWM_API_KEY"].value_or("");
                weather.latitude = (*weatherTable)["OWM_LAT"].value_or(47.6062);
                weather.longitude = (*weatherTable)["OWM_LON"].value_or(-122.3321);
                weather.units = (*weatherTable)["OWM_UNITS"].value_or("imperial");
            }
            
            // Parse display settings
            // if (auto displayTable = config["display"].as_table()) {
            //     display.updateInterval = displayTable->get("update_interval")->value_or(1000);
            //     display.windowWidth = displayTable->get("window_width")->value_or(800);
            //     display.windowHeight = displayTable->get("window_height")->value_or(600);
            //     display.fullscreen = displayTable->get("fullscreen")->value_or(false);
            // }
            
            // Parse network settings
            if (auto networkTable = config["network"].as_table()) {
                network.espIP = (*networkTable)["esp_ip"].value_or("192.168.1.100");
                network.espPort = (*networkTable)["esp_port"].value_or(8080);
            }

            if (auto trainTable = config["train"].as_table()) {
                train.apiKey = (*trainTable)["api_key"].value_or("");
                train.stopId0 = (*trainTable)["stop_id_0"].value_or("");
                train.stopId1 = (*trainTable)["stop_id_1"].value_or("");
                train.apiBase = (*trainTable)["api_base"].value_or("");
            }
            
            std::cout << "Settings loaded successfully from: " << settingsPath << "\n";
            return true;
            
        } catch (const toml::parse_error& err) {
            std::cerr << "Error parsing settings.toml: " << err << "\n";
            return false;
        }
    }
    
    // Save current settings back to file
    bool save() {
        std::filesystem::path settingsPath = getExeDirectory() / "settings.toml";
        
        try {
            toml::table config;
            
            // Weather section
            config.insert_or_assign("weather", toml::table{
                {"OWM_API_KEY", weather.apiKey},
                {"OWM_LAT", weather.latitude},
                {"OWM_LON", weather.longitude},
                {"OWM_UNITS", weather.units}
            });
            
            // Display section
            // config.insert_or_assign("display", toml::table{
            //     {"update_interval", display.updateInterval},
            //     {"window_width", display.windowWidth},
            //     {"window_height", display.windowHeight},
            //     {"fullscreen", display.fullscreen}
            // });
            
            // Network section
            config.insert_or_assign("network", toml::table{
                {"esp_ip", network.espIP},
                {"esp_port", network.espPort},
            });

            config.insert_or_assign("preferences", toml::table{
                {"selected_skin", preferences.selectedSkin},
                {"rotate_180", preferences.rotate180}
            });

            config.insert_or_assign("train", toml::table{
                {"api_key", train.apiKey},
                {"stop_id_0", train.stopId0},
                {"stop_id_1", train.stopId1},
                {"api_base", train.apiBase}
            });
            
            std::ofstream file(settingsPath);
            file << config;
            
            std::cout << "Settings saved to: " << settingsPath << "\n";
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Error saving settings: " << e.what() << "\n";
            return false;
        }
    }
    
private:
    std::filesystem::path getExeDirectory() {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        return std::filesystem::path(exePath).parent_path();
    }
    
    void createDefaultSettings(const std::filesystem::path& path) {
        toml::table config;
        
        config.insert_or_assign("weather", toml::table{
            {"OWM_API_KEY", "YOUR_API_KEY_HERE"},
            {"OWM_LAT", 47.6062},
            {"OWM_LON", -122.3321},
            {"OWM_UNITS", "imperial"}
        });
        
        // config.insert_or_assign("display", toml::table{
        //     {"update_interval", 1000},
        //     {"window_width", 800},
        //     {"window_height", 600},
        //     {"fullscreen", false}
        // });
        
        config.insert_or_assign("network", toml::table{
            {"esp_ip", "192.168.1.100"},
            {"esp_port", 8080},
        });

        config.insert_or_assign("preferences", toml::table{
            {"selected_skin", "Debug"},
            {"rotate_180", false}
        });

        config.insert_or_assign("train", toml::table{
            {"api_key", "YOUR_API_KEY_HERE"},
            {"stop_id_0", "40_99610"},
            {"stop_id_1", "40_99603"},
            {"api_base", "https://api.pugetsound.onebusaway.org"}
        });
        
        std::ofstream file(path);
        file << "# System Monitor Settings\n\n";
        file << config;
    }
};

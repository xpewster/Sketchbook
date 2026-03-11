#pragma once
#include <toml++/toml.h>
#include <windows.h>
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include "log.hpp"

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
        std::string espDrive; // e.g. "E:"
        bool rleEnabled = true;
        ColorMode colorMode = ColorMode::RGB565;
    };

    struct Preferences {
        std::string selectedSkin = "Debug";
        bool rotate180 = false;
        bool showDirtyRects = true;
        bool frameLock = true;
        bool flashMode = false;
        bool frameLockRealTimePreview = false;
        bool startMinimized = false;
        bool closeToTray = true;
        bool autoConnect = false;
        bool autoMemFlash = false;
        bool resetAfterFlash = true;
        uint8_t brightness = 255; // 0-255
        std::string brightnessScheduleStr = ""; // e.g. "19:30={200},21:30={160},06:00={255}"
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
            LOG_INFO << "settings.toml not found, creating default...\n";
            createDefaultSettings(settingsPath);
        }

        LOG_INFO << "Loading settings from: " << settingsPath << "\n";
        
        try {
            auto config = toml::parse_file(settingsPath.string());
            LOG_INFO << "Parsed settings.toml successfully\n";

            if (auto prefTable = config["preferences"].as_table()) {
                preferences.selectedSkin = (*prefTable)["selected_skin"].value_or("Debug");
                preferences.rotate180 = (*prefTable)["rotate_180"].value_or(false);
                preferences.showDirtyRects = (*prefTable)["show_dirty_rects"].value_or(true);
                preferences.frameLock = (*prefTable)["frame_lock"].value_or(true);
                preferences.flashMode = (*prefTable)["flash_mode"].value_or(false);
                preferences.frameLockRealTimePreview = (*prefTable)["frame_lock_real_time_preview"].value_or(false);
                preferences.closeToTray = (*prefTable)["close_to_tray"].value_or(true);
                preferences.autoConnect = (*prefTable)["auto_connect"].value_or(false);
                preferences.startMinimized = (*prefTable)["start_minimized"].value_or(false);
                preferences.autoMemFlash = (*prefTable)["auto_mem_flash"].value_or(false);
                preferences.resetAfterFlash = (*prefTable)["reset_after_flash"].value_or(true);
                preferences.brightness = (*prefTable)["brightness"].value_or(255);
                preferences.brightnessScheduleStr = (*prefTable)["brightness_schedule"].value_or("");
            }
            LOG_INFO << "Loaded preferences: selectedSkin=" << preferences.selectedSkin 
                     << ", rotate180=" << preferences.rotate180
                     << ", showDirtyRects=" << preferences.showDirtyRects
                     << ", frameLock=" << preferences.frameLock
                     << ", flashMode=" << preferences.flashMode
                     << ", frameLockRealTimePreview=" << preferences.frameLockRealTimePreview
                     << ", closeToTray=" << preferences.closeToTray
                     << ", autoConnect=" << preferences.autoConnect
                     << ", startMinimized=" << preferences.startMinimized
                     << ", autoMemFlash=" << preferences.autoMemFlash
                     << ", resetAfterFlash=" << preferences.resetAfterFlash
                     << ", brightness=" << static_cast<int>(preferences.brightness)
                     << ", brightnessSchedule=" << preferences.brightnessScheduleStr
                     << "\n";
            
            // Parse weather settings
            if (auto weatherTable = config["weather"].as_table()) {
                weather.apiKey = (*weatherTable)["OWM_API_KEY"].value_or("");
                weather.latitude = (*weatherTable)["OWM_LAT"].value_or(47.6062);
                weather.longitude = (*weatherTable)["OWM_LON"].value_or(-122.3321);
                weather.units = (*weatherTable)["OWM_UNITS"].value_or("imperial");
            }
            LOG_INFO << "Loaded weather settings: apiKey " << (weather.apiKey.empty() ? "NOT SET" : "SET") 
                     << ", latitude=" << weather.latitude 
                     << ", longitude=" << weather.longitude 
                     << ", units=" << weather.units 
                     << "\n";
            
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
                network.espDrive = (*networkTable)["esp_drive"].value_or("");
                network.rleEnabled = (*networkTable)["rle_enabled"].value_or(true);
                network.colorMode = static_cast<ColorMode>((*networkTable)["color_mode"].value_or(static_cast<int>(ColorMode::RGB565)));
            }
            LOG_INFO << "Loaded network settings: espIP=" << network.espIP 
                     << ", espPort=" << network.espPort 
                     << ", espDrive=" << (network.espDrive.empty() ? "NOT SET" : network.espDrive) 
                     << ", rleEnabled=" << (network.rleEnabled ? "true" : "false")
                     << ", colorMode=" << static_cast<int>(network.colorMode)
                     << "\n";

            if (auto trainTable = config["train"].as_table()) {
                train.apiKey = (*trainTable)["api_key"].value_or("");
                train.stopId0 = (*trainTable)["stop_id_0"].value_or("");
                train.stopId1 = (*trainTable)["stop_id_1"].value_or("");
                train.apiBase = (*trainTable)["api_base"].value_or("");
            }
            LOG_INFO << "Loaded train settings: apiKey " << (train.apiKey.empty() ? "NOT SET" : "SET") 
                     << ", stopId0=" << train.stopId0 
                     << ", stopId1=" << train.stopId1 
                     << ", apiBase=" << train.apiBase 
                     << "\n";
            
            LOG_INFO << "Settings loaded successfully from: " << settingsPath << "\n";
            return true;
            
        } catch (const toml::parse_error& err) {
            LOG_ERROR << "Error parsing settings.toml: " << err << "\n";
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
                {"esp_drive", network.espDrive},
                {"rle_enabled", network.rleEnabled},
                {"color_mode", static_cast<int>(network.colorMode)}
            });
            
            // // Support updating brightness schedule during program execution by retrieving the latest value before save.
            // // Otherwise, the schedule is overwritten on save with the old value. This read value is safe to trust since we don't modify it within the program yet
            // std::filesystem::path settingsPath = getExeDirectory() / "settings.toml";
            // auto latestConfig = toml::parse_file(settingsPath.string());
            // if (auto prefTable = latestConfig["preferences"].as_table()) {
            //     preferences.brightnessScheduleStr = (*prefTable)["brightness_schedule"].value_or("");
            // }

            config.insert_or_assign("preferences", toml::table{
                {"selected_skin", preferences.selectedSkin},
                {"rotate_180", preferences.rotate180},
                {"show_dirty_rects", preferences.showDirtyRects},
                {"frame_lock", preferences.frameLock},
                {"flash_mode", preferences.flashMode},
                {"frame_lock_real_time_preview", preferences.frameLockRealTimePreview},
                {"start_minimized", preferences.startMinimized},
                {"close_to_tray", preferences.closeToTray},
                {"auto_connect", preferences.autoConnect},
                {"auto_mem_flash", preferences.autoMemFlash},
                {"reset_after_flash", preferences.resetAfterFlash},
                {"brightness", preferences.brightness},
                {"brightness_schedule", preferences.brightnessScheduleStr}
            });

            config.insert_or_assign("train", toml::table{
                {"api_key", train.apiKey},
                {"stop_id_0", train.stopId0},
                {"stop_id_1", train.stopId1},
                {"api_base", train.apiBase}
            });
            
            std::ostringstream oss;
            oss << config;
            std::string tomlStr = oss.str();
            insertInlineComment(tomlStr, "brightness_schedule", "Format: 'HH:MM={brightness},HH:MM={brightness},...'");

            std::ofstream file(settingsPath);
            file << tomlStr;
            
            LOG_INFO << "Settings saved to: " << settingsPath << "\n";
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR << "Error saving settings: " << e.what() << "\n";
            return false;
        }
    }
    
private:
    // Inserts a TOML comment before the first occurrence of a key in the serialized output.
    // tomlplusplus doesn't support programmatic comments on nodes, so we post-process the string.
    static void insertInlineComment(std::string& tomlStr, const std::string& key, const std::string& comment) {
        auto pos = tomlStr.find(key);
        if (pos != std::string::npos) {
            auto eol = tomlStr.find('\n', pos);
            if (eol == std::string::npos) eol = tomlStr.size();
            tomlStr.insert(eol, " # " + comment);
        }
    }

    // Inserts a TOML comment before the first occurrence of a key in the serialized output.
    // tomlplusplus doesn't support programmatic comments on nodes, so we post-process the string.
    static void insertCommentBeforeKey(std::string& tomlStr, const std::string& key, const std::string& comment) {
        auto pos = tomlStr.find(key);
        if (pos != std::string::npos) {
            tomlStr.insert(pos, "# " + comment + "\n");
        }
    }

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
            {"rotate_180", false},
            {"show_dirty_rects", true},
            {"start_minimized", false},
            {"close_to_tray", true},
            {"auto_connect", true},
            {"brightness", 255},
            {"brightness_schedule", "19:30={200},21:30={160},06:00={255}"}
        });

        config.insert_or_assign("train", toml::table{
            {"api_key", "YOUR_API_KEY_HERE"},
            {"stop_id_0", "40_99610"},
            {"stop_id_1", "40_99603"},
            {"api_base", "https://api.pugetsound.onebusaway.org"}
        });
        
        std::ostringstream oss;
        oss << config;
        std::string tomlStr = oss.str();
        insertInlineComment(tomlStr, "brightness_schedule", "Format: 'HH:MM={brightness},HH:MM={brightness},...'");

        std::ofstream file(path);
        file << "# System Monitor Settings\n\n";
        file << tomlStr;
    }
};
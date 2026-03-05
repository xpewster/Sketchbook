#pragma once
// config_parser.h - Simple key=value config file parser
// Matches the CircuitPython config.py format

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include "esp_log.h"

static const char* TAG_CFG = "config";

struct FlashConfig {
    std::map<std::string, std::string> values;

    bool load(const char* path) {
        FILE* f = fopen(path, "r");
        if (!f) {
            ESP_LOGE(TAG_CFG, "Cannot open %s", path);
            return false;
        }
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            // Strip newline
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            nl = strchr(line, '\r');
            if (nl) *nl = '\0';

            // Skip blank/comment
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0' || *p == '#') continue;

            // Split on '='
            char* eq = strchr(p, '=');
            if (!eq) continue;

            // Key
            char* kend = eq;
            while (kend > p && (*(kend-1) == ' ' || *(kend-1) == '\t')) kend--;
            std::string key(p, kend - p);

            // Value
            char* vstart = eq + 1;
            while (*vstart == ' ' || *vstart == '\t') vstart++;
            char* vend = vstart + strlen(vstart);
            while (vend > vstart && (*(vend-1) == ' ' || *(vend-1) == '\t')) vend--;
            std::string val(vstart, vend - vstart);

            values[key] = val;
        }
        fclose(f);
        ESP_LOGI(TAG_CFG, "Loaded %d config entries from %s", (int)values.size(), path);
        return !values.empty();
    }

    const char* get_str(const char* key, const char* def = "") const {
        auto it = values.find(key);
        return it != values.end() ? it->second.c_str() : def;
    }

    int get_int(const char* key, int def = 0) const {
        auto it = values.find(key);
        if (it == values.end()) return def;
        return atoi(it->second.c_str());
    }

    float get_float(const char* key, float def = 0.0f) const {
        auto it = values.find(key);
        if (it == values.end()) return def;
        return atof(it->second.c_str());
    }

    bool get_bool(const char* key, bool def = false) const {
        auto it = values.find(key);
        if (it == values.end()) return def;
        const auto& v = it->second;
        return v == "1" || v == "true" || v == "yes" || v == "True";
    }
};

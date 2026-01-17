#pragma once

#include <vector>
#include <string>
#include "../skins/skin.h"

inline int getSkinIndex(std::vector<std::string>& skins, const std::string& name) {
    for (size_t i = 0; i < skins.size(); ++i) {
        if (skins[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1; // Not found
}

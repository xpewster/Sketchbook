#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <chrono>
#include <ctime>
#include "log.hpp"

struct DisplayConfig {
    uint8_t brightness;

    bool operator==(const DisplayConfig& other) const {
        return brightness == other.brightness;
    }
    bool operator!=(const DisplayConfig& other) const {
        return !(*this == other);
    }
};

// Represents minutes since midnight [0, 1440).
struct TimeOfDay {
    uint16_t minutesSinceMidnight;

    bool operator<(const TimeOfDay& other) const {
        return minutesSinceMidnight < other.minutesSinceMidnight;
    }
    bool operator==(const TimeOfDay& other) const {
        return minutesSinceMidnight == other.minutesSinceMidnight;
    }
};

inline TimeOfDay currentTimeOfDay() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm local = *std::localtime(&tt);
    return TimeOfDay{static_cast<uint16_t>(local.tm_hour * 60 + local.tm_min)};
}

/// A daily schedule mapping time-of-day entries to DisplayConfig values.
///
/// The active config at any moment is the one with the greatest start time
/// that is <= the current time. If the current time is before all entries,
/// the last entry of the day (i.e. the one that wraps around midnight) is used.
///
/// Schedule string format: "HH:MM={brightness,...other fields that could be added later},HH:MM={brightness,...},..."
///   e.g. "20:00={200},21:00={150},6:00={255}"
class Schedule {
public:
    Schedule() = default;

    explicit Schedule(const std::string& scheduleStr) {
        parse(scheduleStr);
    }

    /// Return the DisplayConfig that should be active at the given time.
    /// Throws if the schedule is empty.
    DisplayConfig getConfigAt(TimeOfDay now) const {
        if (entries_.empty()) {
            throw std::runtime_error("Schedule is empty");
        }

        // upper_bound gives the first entry strictly after `now`.
        // We want the entry at or before `now`, so we step back one.
        auto it = entries_.upper_bound(now);
        if (it == entries_.begin()) {
            // `now` is before all entries — wrap around to the last entry.
            return entries_.rbegin()->second;
        }
        --it;
        return it->second;
    }

    DisplayConfig getConfigAt(uint8_t hour, uint8_t minute) const {
        return getConfigAt(TimeOfDay{static_cast<uint16_t>(hour * 60 + minute)});
    }

    // Return true if the given time is in the last time period of the day=latest period that starts before 4am.
    bool isLastTimePeriodOfNight(TimeOfDay now) const {
        if (entries_.empty()) return false;

        constexpr TimeOfDay fourAM{4 * 60};

        // Find the entry active just before 4 AM
        auto nightIt = entries_.upper_bound(TimeOfDay{fourAM.minutesSinceMidnight - 1});
        if (nightIt == entries_.begin())
            nightIt = std::prev(entries_.end());
        else
            --nightIt;

        // Find the entry active at `now`
        auto nowIt = entries_.upper_bound(now);
        if (nowIt == entries_.begin())
            nowIt = std::prev(entries_.end());
        else
            --nowIt;

        return nowIt == nightIt;
    }

    DisplayConfig getNextConfigAt(TimeOfDay now) const {
        if (entries_.empty()) {
            throw std::runtime_error("Schedule is empty");
        }

        auto it = entries_.upper_bound(now);
        if (it == entries_.end())
            it = entries_.begin();

        return it->second;
    }

    /// Direct access to the underlying ordered map.
    const std::map<TimeOfDay, DisplayConfig>& entries() const {
        return entries_;
    }

    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }

private:
    std::map<TimeOfDay, DisplayConfig> entries_;

    void parse(const std::string& str) {
        if (str.empty()) {
            LOG_INFO << "Empty brightness schedule string, ignoring\n";
            return;
        }
        try {
            size_t pos = 0;
            while (pos < str.size()) {
                size_t comma = str.find(',', pos);
                if (comma == std::string::npos) comma = str.size();

                std::string token = str.substr(pos, comma - pos);
                parseEntry(token);

                pos = comma + 1;
            }
        } catch (const std::runtime_error& e) {
            LOG_WARN << "Invalid brightness schedule string, ignoring: " << e.what();
            entries_.clear();
        }
    }

    void parseEntry(const std::string& token) {
        // Expected: "H:MM={brightness}" or "HH:MM={brightness}"
        size_t eq = token.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("Invalid schedule entry (missing '='): " + token);
        }

        std::string timePart = token.substr(0, eq);
        std::string valuePart = token.substr(eq + 1);

        TimeOfDay tod = parseTime(timePart);
        DisplayConfig cfg = parseConfig(valuePart);
        entries_[tod] = cfg;
    }

    static TimeOfDay parseTime(const std::string& s) {
        unsigned h = 0, m = 0;
        if (std::sscanf(s.c_str(), "%u:%u", &h, &m) != 2) {
            throw std::runtime_error("Invalid time format: " + s);
        }
        if (h >= 24 || m >= 60) {
            throw std::runtime_error("Time out of range: " + s);
        }
        return TimeOfDay{static_cast<uint16_t>(h * 60 + m)};
    }

    static DisplayConfig parseConfig(const std::string& s) {
        // Strip surrounding braces: "{brightness}" or "{brightness,...}"
        if (s.size() < 2 || s.front() != '{' || s.back() != '}') {
            throw std::runtime_error("Invalid config format (expected {…}): " + s);
        }
        std::string inner = s.substr(1, s.size() - 2);

        // Split on ',' and parse fields positionally.
        // Field 0: brightness
        unsigned brightness = 0;
        if (std::sscanf(inner.c_str(), "%u", &brightness) != 1) {
            throw std::runtime_error("Failed to parse brightness from: " + inner);
        }
        if (brightness > 255) {
            throw std::runtime_error("Brightness out of range: " + std::to_string(brightness));
        }

        DisplayConfig cfg{};
        cfg.brightness = static_cast<uint8_t>(brightness);

        // Future fields would be parsed here from subsequent comma-separated values.
        return cfg;
    }
};
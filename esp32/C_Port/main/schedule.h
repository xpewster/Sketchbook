#pragma once
// schedule.h 

#include "config.h"
#include "storage.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <ctime>

static const char* TAG_SCHED = "schedule";

#define SCHEDULE_MAX_ENTRIES 16

struct SchedEntry {
    uint16_t minutes;    // minutes since midnight
    uint8_t  brightness;
};

/// Parse "19:30={210},20:30={170},..." into a sorted array.
/// Returns number of entries parsed.
static int parse_brightness_schedule(const char* str, SchedEntry* out, int max_entries) {
    int count = 0;
    const char* p = str;

    while (*p && count < max_entries) {
        unsigned h, m, b;
        int consumed = 0;
        if (sscanf(p, "%u:%u={%u}%n", &h, &m, &b, &consumed) >= 3
            && h < 24 && m < 60 && b <= 255)
        {
            out[count].minutes = h * 60 + m;
            out[count].brightness = (uint8_t)b;
            count++;
            p += consumed;
            if (*p == ',') p++;
        } else {
            // skip to next comma or end
            const char* next = strchr(p, ',');
            if (next) p = next + 1;
            else break;
        }
    }

    // Insertion sort by minutes (tiny array, doesn't matter)
    for (int i = 1; i < count; i++) {
        SchedEntry tmp = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].minutes > tmp.minutes) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = tmp;
    }

    return count;
}

/// Find the active brightness for the given minutes-since-midnight.
/// largest entry <= now, wrapping to last entry.
static uint8_t lookup_brightness(const SchedEntry* entries, int count, uint16_t now_minutes) {
    // Walk backwards to find first entry <= now
    for (int i = count - 1; i >= 0; i--) {
        if (entries[i].minutes <= now_minutes) {
            return entries[i].brightness;
        }
    }
    // Before all entries — wrap to last (latest time of day)
    return entries[count - 1].brightness;
}

static bool rtc_time_is_set() {
    if (!(time(nullptr) > 1700000000)) {  // past Nov 2023
        ESP_LOGW(TAG_SCHED, "RTC time not set (time=%ld)", time(nullptr));
        return false;
    }
    return true;
}

/// Read schedule.txt, return brightness for current RTC time.
/// Falls back to `fallback` if anything goes wrong or RTC isn't set.
inline uint8_t load_scheduled_brightness(uint8_t fallback) {
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    ESP_LOGI(TAG_SCHED, "Loading scheduled brightness (fallback=%u) with time %02d:%02d", fallback, local.tm_hour, local.tm_min);
    if (!rtc_time_is_set()) {
        ESP_LOGW(TAG_SCHED, "RTC not set, using fallback brightness %u", fallback);
        return fallback;
    }

    FlashConfig cfg;
    if (!cfg.load(FLASH_SCHEDULE_FILE)) {
        ESP_LOGW(TAG_SCHED, "No schedule file, using fallback %u", fallback);
        return fallback;
    }

    const char* sched_str = cfg.get_str("brightness", "");
    if (sched_str[0] == '\0') {
        ESP_LOGW(TAG_SCHED, "No 'brightness' key in schedule");
        return fallback;
    }

    SchedEntry entries[SCHEDULE_MAX_ENTRIES];
    int count = parse_brightness_schedule(sched_str, entries, SCHEDULE_MAX_ENTRIES);
    if (count == 0) {
        ESP_LOGW(TAG_SCHED, "Schedule parsed but empty");
        return fallback;
    }

    uint16_t now_min = local.tm_hour * 60 + local.tm_min;

    uint8_t brightness = lookup_brightness(entries, count, now_min);
    ESP_LOGI(TAG_SCHED, "Schedule: %02d:%02d -> brightness %u",
             local.tm_hour, local.tm_min, brightness);
    return brightness;
}
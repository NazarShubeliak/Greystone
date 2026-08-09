#pragma once
#include <string>
#include <cstdio>

struct WorldTime {
    // Total game minutes elapsed. Start at 07:00, Day 1.
    int minutes = 7 * 60;

    // 1 action = 1 game minute (1440 actions per full day).
    static constexpr int MINS_PER_ACTION = 1;

    // A year is 12 30-day months — astronomically long in action-count on
    // purpose (docs/village.md forbids artificial age acceleration). Used by
    // main.cpp's tickAging() to advance Actor::age once per elapsed year.
    static constexpr int MINUTES_PER_YEAR = 60 * 24 * 30 * 12; // 518400

    void advance() { minutes += MINS_PER_ACTION; }

    int minute() const { return minutes % 60; }
    int hour()   const { return (minutes / 60) % 24; }
    int day()    const { return (minutes / (60 * 24)) % 30 + 1; }
    int month()  const { return (minutes / (60 * 24 * 30)) % 12; }
    // 0=Spring  1=Summer  2=Autumn  3=Winter
    int season() const { return month() / 3; }

    const char* seasonName() const {
        static const char* s[] = {"Spring", "Summer", "Autumn", "Winter"};
        return s[season()];
    }

    const char* monthName() const {
        static const char* m[] = {
            "Seedling", "Blossom",     "Verdance",    // Spring
            "Sunpeak",  "Hearthfire",  "Embertide",   // Summer
            "Ashfall",  "Harvestmoon", "Dimming",     // Autumn
            "Frostfall","Glacial",     "Darkest"      // Winter
        };
        return m[month()];
    }

    // Sunrise / sunset hours vary by season.
    float sunriseHour() const {
        static const float rise[] = {6.0f, 5.0f, 7.0f, 8.0f};
        return rise[season()];
    }
    float sunsetHour() const {
        static const float set[] = {20.0f, 21.0f, 19.0f, 16.0f};
        return set[season()];
    }

    // 0.0 = full daylight, 1.0 = full night.
    // Dawn and dusk each last 1 hour.
    float darkness() const {
        float fh      = hour() + minute() / 60.0f;
        float rise    = sunriseHour();
        float set_    = sunsetHour();
        float dawnEnd = rise;
        float dawnBeg = rise - 1.0f;
        float duskBeg = set_;
        float duskEnd = set_ + 1.0f;

        if (fh >= dawnEnd && fh < duskBeg) return 0.0f;           // full day
        if (fh >= dawnBeg && fh < dawnEnd) return 1.0f - (fh - dawnBeg); // dawn 1→0
        if (fh >= duskBeg && fh < duskEnd) return fh - duskBeg;           // dusk 0→1
        return 1.0f;                                                // night
    }

    // FOV radius: 16 at full day, 3 at full night (without light).
    // lightBonus is added scaled by darkness — light sources only help in the dark.
    int viewRadius(int lightBonus = 0) const {
        float d    = darkness();
        int   base = (int)(16.0f - d * 13.0f);          // 16 day → 3 night
        int   lit  = base + (int)(lightBonus * d);       // light only matters in dark
        return std::max(3, std::min(16, lit));
    }

    // True if the given hour was just crossed going forward by MINS_PER_ACTION.
    // Used to fire one-per-hour events: compare prevMinutes with current.
    bool crossedHour(int h, int prevMinutes) const {
        int target = h * 60;
        return prevMinutes < target && minutes >= target;
    }

    std::string timeStr() const {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", hour(), minute());
        return buf;
    }

    std::string dateStr() const {
        return "Day " + std::to_string(day())
             + "  " + monthName()
             + " (" + seasonName() + ")";
    }
};

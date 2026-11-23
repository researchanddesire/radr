#pragma once

#include <climits>
#include <string>

// Legacy text commands the RADR writes to the OSSM command characteristic.
// The OSSM accepts exactly (OSSM software/firmware/src/command/
// command_pattern.hpp, isValidBleCommand):
//   go:(simplePenetration|strokeEngine|streaming|menu|restart|update|pairing)
//   set:(speed|stroke|depth|sensation|buffer|pattern):<digits, 0..100>
// Anything else is answered with "fail:<command>" and ignored, so these
// builders only produce that grammar. Tested natively
// (test/test_ossm_command_encoding).
namespace ossm_command {

enum class Setting { Speed, Stroke, Depth, Sensation, Pattern };

inline const char *settingName(Setting setting) {
    switch (setting) {
        case Setting::Speed: return "speed";
        case Setting::Stroke: return "stroke";
        case Setting::Depth: return "depth";
        case Setting::Sensation: return "sensation";
        case Setting::Pattern: return "pattern";
    }
    return "speed";
}

inline int clampPercent(int value) {
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}

// set:<setting>:<value>, the value clamped to the 0..100 the OSSM accepts
// (the OSSM ignores anything outside it rather than clamping).
inline std::string set(Setting setting, int value) {
    return std::string("set:") + settingName(setting) + ":" +
           std::to_string(clampPercent(value));
}

inline bool isGoTarget(const std::string &mode) {
    static const char *const targets[] = {"simplePenetration", "strokeEngine",
                                          "streaming",         "menu",
                                          "restart",           "update",
                                          "pairing"};
    for (const char *target : targets) {
        if (mode == target) return true;
    }
    return false;
}

// go:<mode> for a mode the OSSM knows; empty for anything else (the OSSM
// would reject it).
inline std::string go(const std::string &mode) {
    return isGoTarget(mode) ? "go:" + mode : std::string();
}

}  // namespace ossm_command

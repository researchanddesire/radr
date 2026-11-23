#pragma once

#include <ArduinoJson.h>

#include <string>

// Keeps a connected OSSM in the mode the RADR asked for.
//
// The OSSM ignores a mode request (go:strokeEngine, go:simplePenetration,
// go:streaming) while it homes, and a request that finds it unhomed starts a
// home. Either way the home ends in the OSSM menu: the OSSM never enters a
// mode on its own after homing. A RADR that connected during homing, or asked
// for a mode on an unhomed OSSM, would otherwise show its control screen
// while the OSSM sits in its menu and ignores the knobs.
//
// No hardware dependencies, so it is tested natively.
namespace ossm_mode_follow {

// The follower gives up after this long. Two homing passes may take up to
// 40 s each before the OSSM reports a homing error.
constexpr uint32_t kFollowMs = 150000;
// Mode requests sent while the OSSM is out of homing and not yet in the mode.
constexpr int kMaxRequests = 3;
// First state check after a mode request.
constexpr uint32_t kStartDelayMs = 1500;
// Polling interval while the OSSM homes.
constexpr uint32_t kHomingPollMs = 700;
// Time the OSSM gets to act on a request before it is checked again.
constexpr uint32_t kRequestSettleMs = 500;

enum class Step {
    Ready,          // the OSSM is in the wanted mode
    WaitForHoming,  // the OSSM is homing (or about to): ask again afterwards
    Request,        // send the mode request
    GiveUp,         // stop following
};

inline bool startsWith(const std::string &text, const char *prefix) {
    return text.rfind(prefix, 0) == 0;
}

/// Next step for an OSSM in `state` when the RADR wants `mode`
/// ("strokeEngine", "simplePenetration" or "streaming"; empty for none).
/// `requestsSent` counts requests since `mode` was set.
inline Step next(const std::string &state, const std::string &mode,
                 int requestsSent) {
    if (mode.empty()) return Step::GiveUp;
    if (startsWith(state, mode.c_str())) return Step::Ready;
    // In the error pages a press opens help and the next one restarts the
    // OSSM. The OSSM shows its error; never press through it from here.
    if (startsWith(state, "error")) return Step::GiveUp;
    // "idle" is the OSSM's boot state just before it starts homing.
    if (state == "idle" || startsWith(state, "homing"))
        return Step::WaitForHoming;
    if (requestsSent >= kMaxRequests) return Step::GiveUp;
    return Step::Request;
}

/// State name from a RAD BLE state.read result: {"state":"menu.idle"} or
/// {"state":{"state":"menu.idle",...}}. False when there is none.
inline bool parseStateName(const std::string &resultJson, std::string &state) {
    JsonDocument document;
    if (deserializeJson(document, resultJson) != DeserializationError::Ok)
        return false;
    JsonVariantConst value = document["state"];
    if (value.is<JsonObjectConst>()) value = value["state"];
    if (!value.is<const char *>()) return false;
    state = value.as<const char *>();
    return !state.empty();
}

}  // namespace ossm_mode_follow

#include <unity.h>

#include <climits>
#include <cstring>
#include <string>

#include "ossm_command_logic.h"

using ossm_command::Setting;

namespace {

// The OSSM's acceptance rules for the legacy command characteristic, copied
// from OSSM software/firmware/src/command/command_pattern.hpp
// (isValidBleCommand) plus its value range check (commands.hpp
// setCommandValue: 0..100 written without leading zeros). Keep in step.
bool startsWith(const std::string &text, const char *prefix) {
    return text.rfind(prefix, 0) == 0;
}

bool isDigits(const std::string &text, size_t from, size_t to) {
    if (from >= to) return false;
    for (size_t i = from; i < to; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

bool ossmAccepts(const std::string &command) {
    static const char *const goTargets[] = {"simplePenetration", "strokeEngine",
                                            "streaming",         "menu",
                                            "restart",           "update",
                                            "pairing"};
    static const char *const settings[] = {"speed",     "stroke", "depth",
                                           "sensation", "buffer", "pattern"};
    if (startsWith(command, "go:")) {
        for (const char *target : goTargets) {
            if (command.compare(3, std::string::npos, target) == 0) return true;
        }
        return false;
    }
    if (startsWith(command, "set:")) {
        const size_t colon = command.find(':', 4);
        if (colon == std::string::npos ||
            !isDigits(command, colon + 1, command.size()))
            return false;
        const std::string value = command.substr(colon + 1);
        const long number = std::stol(value);
        if (number < 0 || number > 100 || value != std::to_string(number))
            return false;
        for (const char *setting : settings) {
            if (command.compare(4, colon - 4, setting) == 0) return true;
        }
    }
    return false;
}

void test_set_commands_use_ossm_names() {
    TEST_ASSERT_EQUAL_STRING("set:speed:42",
                             ossm_command::set(Setting::Speed, 42).c_str());
    TEST_ASSERT_EQUAL_STRING("set:stroke:7",
                             ossm_command::set(Setting::Stroke, 7).c_str());
    TEST_ASSERT_EQUAL_STRING("set:depth:100",
                             ossm_command::set(Setting::Depth, 100).c_str());
    TEST_ASSERT_EQUAL_STRING("set:sensation:0",
                             ossm_command::set(Setting::Sensation, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("set:pattern:3",
                             ossm_command::set(Setting::Pattern, 3).c_str());
}

void test_set_values_are_clamped_to_what_the_ossm_accepts() {
    // The OSSM ignores out-of-range values instead of clamping them, so a
    // stray encoder value must not turn into a lost command.
    TEST_ASSERT_EQUAL_STRING("set:speed:0",
                             ossm_command::set(Setting::Speed, -1).c_str());
    TEST_ASSERT_EQUAL_STRING("set:speed:100",
                             ossm_command::set(Setting::Speed, 101).c_str());
    TEST_ASSERT_EQUAL_STRING("set:depth:0",
                             ossm_command::set(Setting::Depth, INT_MIN).c_str());
    TEST_ASSERT_EQUAL_STRING("set:depth:100",
                             ossm_command::set(Setting::Depth, INT_MAX).c_str());
}

void test_every_set_command_is_accepted_by_the_ossm() {
    const Setting all[] = {Setting::Speed, Setting::Stroke, Setting::Depth,
                           Setting::Sensation, Setting::Pattern};
    for (Setting setting : all) {
        for (int value = -3; value <= 103; value++) {
            const std::string command = ossm_command::set(setting, value);
            TEST_ASSERT_TRUE_MESSAGE(ossmAccepts(command), command.c_str());
        }
    }
}

void test_go_commands_for_known_modes() {
    const char *modes[] = {"strokeEngine", "simplePenetration", "streaming",
                           "menu",         "restart",           "update",
                           "pairing"};
    for (const char *mode : modes) {
        const std::string command = ossm_command::go(mode);
        TEST_ASSERT_EQUAL_STRING((std::string("go:") + mode).c_str(),
                                 command.c_str());
        TEST_ASSERT_TRUE_MESSAGE(ossmAccepts(command), command.c_str());
    }
}

void test_go_rejects_unknown_or_malformed_modes() {
    // The OSSM would answer "fail:" (and old firmware fell back to the menu).
    const char *modes[] = {"",          "strokeengine", "strokeEngine ",
                           "menu\n",    "go:menu",      "stroke Engine",
                           "streaming:1"};
    for (const char *mode : modes) {
        TEST_ASSERT_TRUE_MESSAGE(ossm_command::go(mode).empty(), mode);
    }
    const std::string oversized(600, 'a');
    TEST_ASSERT_TRUE(ossm_command::go(oversized).empty());
}

void test_grammar_copy_rejects_what_the_ossm_rejects() {
    // Sanity check of the copied rules themselves.
    const char *rejected[] = {"set:speed:101", "set:speed:-1", "set:speed:050",
                              "set:speed:",    "set:wobble:5", "go:dance",
                              "stream:50",     "SET:speed:5",  ""};
    for (const char *command : rejected) {
        TEST_ASSERT_FALSE_MESSAGE(ossmAccepts(command), command);
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_set_commands_use_ossm_names);
    RUN_TEST(test_set_values_are_clamped_to_what_the_ossm_accepts);
    RUN_TEST(test_every_set_command_is_accepted_by_the_ossm);
    RUN_TEST(test_go_commands_for_known_modes);
    RUN_TEST(test_go_rejects_unknown_or_malformed_modes);
    RUN_TEST(test_grammar_copy_rejects_what_the_ossm_rejects);
    return UNITY_END();
}

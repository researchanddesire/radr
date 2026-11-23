#include <unity.h>

#include <string>

#include "devices/researchAndDesire/ossm/ossm_state_parser.hpp"

static bool parse(const char *json, OssmStateInfo &out) {
    return parseOssmState(json, std::strlen(json), out);
}

void test_parses_base_state_without_optional_fields() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(
        R"({"timestamp":1,"state":"menu.idle","speed":0,"stroke":50,"sensation":50,"depth":10,"buffer":100,"pattern":0,"position":0.00,"sessionId":"x"})",
        info));
    TEST_ASSERT_EQUAL_STRING("menu.idle", info.state.c_str());
    TEST_ASSERT_TRUE(info.error.empty());
    TEST_ASSERT_TRUE(info.pairingCode.empty());
    TEST_ASSERT_FALSE(info.isPaired);
}

void test_parses_update_failure() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(R"({"state":"update.failed","error":"low-memory"})", info));
    TEST_ASSERT_EQUAL_STRING("update.failed", info.state.c_str());
    TEST_ASSERT_EQUAL_STRING("low-memory", info.error.c_str());
}

void test_parses_pairing_fields() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(R"({"state":"pairing.idle","pairingCode":"AB12CD"})", info));
    TEST_ASSERT_EQUAL_STRING("AB12CD", info.pairingCode.c_str());
    TEST_ASSERT_FALSE(info.isPaired);
    TEST_ASSERT_TRUE(parse(R"({"state":"pairing.success.idle","isPaired":true})", info));
    TEST_ASSERT_TRUE(info.isPaired);
}

void test_parses_target_version() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(R"({"state":"update.available","targetVersion":"1.0.58"})", info));
    TEST_ASSERT_EQUAL_STRING("1.0.58", info.targetVersion.c_str());
    TEST_ASSERT_TRUE(parse(R"({"state":"update.idle"})", info));
    TEST_ASSERT_TRUE(info.targetVersion.empty());
}

void test_rejects_garbage_and_missing_state() {
    OssmStateInfo info;
    TEST_ASSERT_FALSE(parse("not json", info));
    TEST_ASSERT_FALSE(parse(R"({"speed":10})", info));
    TEST_ASSERT_FALSE(parse("ok:go:update", info));
}

void test_state_prefix_helper() {
    TEST_ASSERT_TRUE(ossmStateStartsWith("update.installing", "update"));
    TEST_ASSERT_TRUE(ossmStateStartsWith("pairing.success.idle", "pairing"));
    TEST_ASSERT_FALSE(ossmStateStartsWith("menu.idle", "update"));
}

void test_error_reasons_are_user_text() {
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("wifi")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("low-memory")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("check-failed")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("install-failed")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("pairing-failed")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("something-new")));
}

void test_pairing_info_fields() {
    const std::string info = "AA:BB:CC:DD:EE:FF;ESP32;1;0123456789abcdef0123456789abcdef;1.2.3";
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", pairingInfoField(info, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("1.2.3", pairingInfoField(info, 4).c_str());
    TEST_ASSERT_EQUAL_STRING("", pairingInfoField(info, 9).c_str());
    TEST_ASSERT_TRUE(pairingInfoHasWifi(info));
    TEST_ASSERT_FALSE(pairingInfoHasWifi("AA;ESP32;0;md5;1.0.0"));
    TEST_ASSERT_FALSE(pairingInfoHasWifi(""));
}

void test_rejects_truncated_state() {
    const char *full = R"({"state":"strokeEngine.idle","speed":10,"error":"wifi"})";
    OssmStateInfo info;
    for (size_t cut = 0; cut < std::strlen(full); cut++) {
        TEST_ASSERT_FALSE(parseOssmState(full, cut, info));
    }
    TEST_ASSERT_TRUE(parseOssmState(full, std::strlen(full), info));
}

void test_rejects_wrong_types() {
    OssmStateInfo info;
    TEST_ASSERT_FALSE(parse(R"({"state":5})", info));
    TEST_ASSERT_FALSE(parse(R"({"state":null})", info));
    TEST_ASSERT_FALSE(parse(R"(["state","menu"])", info));
    TEST_ASSERT_FALSE(parse("", info));
    TEST_ASSERT_TRUE(parse(R"({"state":"menu","isPaired":"true","error":7,"pairingCode":false})", info));
    TEST_ASSERT_FALSE(info.isPaired);
    TEST_ASSERT_TRUE(info.error.empty());
    TEST_ASSERT_TRUE(info.pairingCode.empty());
}

void test_parses_unterminated_buffer_by_length() {
    const char text[] = R"({"state":"homing.forward"})";
    char buffer[sizeof(text) + 4];
    std::memcpy(buffer, text, sizeof(text) - 1);
    std::memset(buffer + sizeof(text) - 1, '}', 5);
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parseOssmState(buffer, sizeof(text) - 1, info));
    TEST_ASSERT_EQUAL_STRING("homing.forward", info.state.c_str());
}

void test_oversized_state_is_copied_whole() {
    const std::string big(3000, 's');
    const std::string json = "{\"state\":\"" + big + "\",\"extra\":[1,2,3]}";
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parseOssmState(json.data(), json.size(), info));
    TEST_ASSERT_EQUAL(big.size(), info.state.size());
}

void test_garbage_inside_a_long_read_is_rejected() {
    // NimBLE once returned long reads cut short with a garbage tail
    // (device.cpp): the object never closes, so the read is rejected.
    std::string json = R"({"state":"menu.idle","spe)";
    json += std::string("\xff\x00\x13garbage", 10);
    OssmStateInfo info;
    TEST_ASSERT_FALSE(parseOssmState(json.data(), json.size(), info));
}

void test_bytes_after_a_complete_object_are_ignored() {
    // ArduinoJson stops at the end of the first value; what follows a
    // complete object cannot change the parsed fields.
    std::string json = R"({"state":"menu.idle","speed":0})";
    json += std::string("\xff\x00\x13garbage", 10);
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parseOssmState(json.data(), json.size(), info));
    TEST_ASSERT_EQUAL_STRING("menu.idle", info.state.c_str());
}

void test_pairing_info_handles_odd_input() {
    TEST_ASSERT_EQUAL_STRING("", pairingInfoField(";;;;", 2).c_str());
    TEST_ASSERT_EQUAL_STRING("", pairingInfoField("a;b", -1).c_str());
    TEST_ASSERT_EQUAL_STRING("b", pairingInfoField("a;b;", 1).c_str());
    const std::string longInfo(5000, ';');
    TEST_ASSERT_EQUAL_STRING("", pairingInfoField(longInfo, 4000).c_str());
    TEST_ASSERT_FALSE(pairingInfoHasWifi("a;b;11;d"));
    TEST_ASSERT_FALSE(pairingInfoHasWifi("a;b; 1;d"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_base_state_without_optional_fields);
    RUN_TEST(test_parses_update_failure);
    RUN_TEST(test_parses_pairing_fields);
    RUN_TEST(test_parses_target_version);
    RUN_TEST(test_rejects_garbage_and_missing_state);
    RUN_TEST(test_state_prefix_helper);
    RUN_TEST(test_error_reasons_are_user_text);
    RUN_TEST(test_pairing_info_fields);
    RUN_TEST(test_rejects_truncated_state);
    RUN_TEST(test_rejects_wrong_types);
    RUN_TEST(test_parses_unterminated_buffer_by_length);
    RUN_TEST(test_oversized_state_is_copied_whole);
    RUN_TEST(test_garbage_inside_a_long_read_is_rejected);
    RUN_TEST(test_bytes_after_a_complete_object_are_ignored);
    RUN_TEST(test_pairing_info_handles_odd_input);
    return UNITY_END();
}

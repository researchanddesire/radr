#include <unity.h>

#include <cstring>
#include <string>

#include "ossm_rad_ble_logic.h"
#include "text_fit_logic.h"

using namespace ossm_rad_ble;

namespace {

ReplyKind parse(const char *json, uint32_t id, Reply &reply) {
    return parseReply(json, strlen(json), id, reply);
}

void test_builds_read_request_without_args_or_lease() {
    const std::string request =
        buildRequest(7, "setting.read", "setting.machine.maxRpm", "", 0);
    TEST_ASSERT_EQUAL_STRING(
        R"({"v":1,"id":7,"op":"setting.read","path":"setting.machine.maxRpm"})",
        request.c_str());
}

void test_builds_write_request_with_args_and_lease() {
    const std::string request =
        buildRequest(9, "setting.write", "setting.machine.maxStrokeMm",
                     valueArgs(300), 1234);
    TEST_ASSERT_EQUAL_STRING(
        R"({"v":1,"id":9,"op":"setting.write","path":"setting.machine.maxStrokeMm","args":{"value":300},"lease":1234})",
        request.c_str());
}

void test_builds_acquire_request_without_path() {
    const std::string request =
        buildRequest(1, "control.acquire", nullptr, ttlArgs(5), 0);
    TEST_ASSERT_EQUAL_STRING(
        R"({"v":1,"id":1,"op":"control.acquire","args":{"ttl":5}})",
        request.c_str());
}

void test_ignores_reply_for_another_id() {
    Reply reply;
    TEST_ASSERT_EQUAL(
        ReplyKind::Ignore,
        parse(R"({"v":1,"id":4,"stage":"completed","ok":true})", 5, reply));
}

void test_ignores_non_json_and_missing_id() {
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Ignore, parse("ok:go:menu", 1, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"stage":"completed","ok":true})", 1, reply));
}

void test_skips_accepted_stage() {
    Reply reply;
    TEST_ASSERT_EQUAL(
        ReplyKind::Accepted,
        parse(R"({"v":1,"id":3,"stage":"accepted","ok":true})", 3, reply));
}

void test_parses_completed_setting_read() {
    Reply reply;
    TEST_ASSERT_EQUAL(
        ReplyKind::Terminal,
        parse(R"({"v":1,"id":2,"stage":"completed","ok":true,"code":"ok","stateBefore":"strokeEngine.idle","result":{"value":300,"active":500,"restartRequired":true}})",
              2, reply));
    TEST_ASSERT_TRUE(reply.ok);
    TEST_ASSERT_EQUAL_STRING("ok", reply.code.c_str());

    SettingRead read;
    TEST_ASSERT_TRUE(parseSettingRead(reply.resultJson, read));
    TEST_ASSERT_EQUAL_FLOAT(300.0f, read.value);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, read.active);
    TEST_ASSERT_TRUE(read.restartRequired);
}

void test_setting_read_without_active_uses_value() {
    SettingRead read;
    TEST_ASSERT_TRUE(parseSettingRead(R"({"value":2.5})", read));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, read.active);
    TEST_ASSERT_FALSE(read.restartRequired);
    TEST_ASSERT_FALSE(parseSettingRead(R"({"value":"x"})", read));
}

void test_parses_failure_code_and_message() {
    Reply reply;
    TEST_ASSERT_EQUAL(
        ReplyKind::Terminal,
        parse(R"({"v":1,"id":6,"stage":"failed","ok":false,"code":"invalid_value","message":"Max stroke must be longer than 50 mm"})",
              6, reply));
    TEST_ASSERT_FALSE(reply.ok);
    TEST_ASSERT_EQUAL_STRING("invalid_value", reply.code.c_str());
    TEST_ASSERT_EQUAL_STRING("Max stroke must be longer than 50 mm",
                             reply.message.c_str());
    TEST_ASSERT_FALSE(meansFirmwareTooOld(reply));
}

void test_unknown_path_and_timeout_mean_old_firmware() {
    Reply reply;
    parse(R"({"v":1,"id":8,"stage":"failed","ok":false,"code":"unknown_path"})",
          8, reply);
    TEST_ASSERT_TRUE(meansFirmwareTooOld(reply));
    TEST_ASSERT_TRUE(meansFirmwareTooOld(failedReply("timeout", "")));
}

void test_parses_lease_token() {
    uint32_t lease = 0;
    TEST_ASSERT_TRUE(parseLease(R"({"lease":3735928559,"ttlMs":5000})", lease));
    TEST_ASSERT_EQUAL_UINT32(3735928559u, lease);
    TEST_ASSERT_FALSE(parseLease(R"({"ttlMs":5000})", lease));
}

void test_default_geometry_is_valid() {
    TEST_ASSERT_EQUAL(GeometryError::None, validateGeometry(MachineConfig{}));
}

void test_geometry_errors_match_ossm_messages() {
    MachineConfig config;
    config.maxStrokeMm = 40;
    TEST_ASSERT_EQUAL(GeometryError::StrokeTooShort, validateGeometry(config));
    TEST_ASSERT_EQUAL_STRING("Max stroke must be longer than 50 mm",
                             geometryErrorMessage(validateGeometry(config)));

    config = MachineConfig{};
    config.stepsPerRev = 40;  // 1 step/mm
    TEST_ASSERT_EQUAL(GeometryError::StepsPerMmTooLow,
                      validateGeometry(config));

    config = MachineConfig{};
    config.maxRpm = 10000;  // 133333 steps/s
    TEST_ASSERT_EQUAL(GeometryError::StepRateTooHigh, validateGeometry(config));

    config = MachineConfig{};
    config.beltPitchMm = 0;
    TEST_ASSERT_EQUAL(GeometryError::NotPositive, validateGeometry(config));

    config = MachineConfig{};
    config.stepsPerRev = 800.5f;
    TEST_ASSERT_EQUAL(GeometryError::NotWholeNumber, validateGeometry(config));
}

void test_encoder_round_trip_uses_field_precision() {
    TEST_ASSERT_EQUAL(25, toEncoder(2, 2.5f));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, fromEncoder(2, 25));
    TEST_ASSERT_EQUAL(800, toEncoder(0, 800.0f));
    TEST_ASSERT_EQUAL(1, toEncoder(4, -3.0f));  // clamped to range
}

void test_formats_labels_with_units() {
    TEST_ASSERT_EQUAL_STRING("Max stroke: 500 mm",
                             formatField(4, 500.0f).c_str());
    TEST_ASSERT_EQUAL_STRING("Belt pitch: 2.5 mm",
                             formatField(2, 2.5f).c_str());
    TEST_ASSERT_EQUAL_STRING("Steps/rev: 800", formatField(0, 800.0f).c_str());
}

void test_plan_writes_skips_unchanged_fields() {
    MachineConfig stored;
    MachineConfig target = stored;
    std::array<int, kFieldCount> order{};
    TEST_ASSERT_EQUAL(0, planWrites(stored, target, order));

    target.maxStrokeMm = 300;
    TEST_ASSERT_EQUAL(1, planWrites(stored, target, order));
    TEST_ASSERT_EQUAL(4, order[0]);
}

void test_plan_writes_orders_dependent_fields() {
    // Doubling steps/rev before lowering rpm would pass 100000 steps/s, so
    // only the rpm-first order keeps every intermediate valid.
    MachineConfig stored;
    stored.maxRpm = 7000;  // 800 * 7000 / 60 = 93333 steps/s
    MachineConfig target = stored;
    target.stepsPerRev = 1600;
    target.maxRpm = 3000;  // 1600 * 3000 / 60 = 80000 steps/s
    std::array<int, kFieldCount> order{};
    TEST_ASSERT_EQUAL(2, planWrites(stored, target, order));
    TEST_ASSERT_EQUAL(3, order[0]);  // maxRpm first
    TEST_ASSERT_EQUAL(0, order[1]);
}

void test_plan_writes_rejects_invalid_target() {
    MachineConfig stored;
    MachineConfig target = stored;
    target.maxStrokeMm = 40;
    std::array<int, kFieldCount> order{};
    TEST_ASSERT_EQUAL(-1, planWrites(stored, target, order));
}

void test_pause_first_falls_back_to_builtin_font() {
    // "Pause First" in FreeSans9pt7b measures about 90 px; the Menu button
    // is 90 px wide.
    TEST_ASSERT_FALSE(text_fit::fitsButton(90, 90));
    const int fallbackWidth = text_fit::builtinTextWidth(strlen("Pause First"));
    TEST_ASSERT_EQUAL(66, fallbackWidth);
    TEST_ASSERT_TRUE(text_fit::fitsButton(fallbackWidth, 90));
}

void test_short_labels_keep_button_font() {
    TEST_ASSERT_TRUE(text_fit::fitsButton(44, 90));  // "Menu"
    TEST_ASSERT_TRUE(text_fit::fitsButton(62, 70));
    TEST_ASSERT_FALSE(text_fit::fitsButton(63, 70));
}

void test_builtin_text_is_vertically_centred() {
    TEST_ASSERT_EQUAL(211, text_fit::builtinTextTop(200, 30));
}

// --- Malformed, truncated and oversized replies (indications are cut at the
// MTU, and anything can arrive on the characteristic) --------------------

void test_ignores_truncated_reply() {
    const char *full = R"({"v":1,"id":5,"stage":"completed","ok":true,"result":{"state":"menu.idle"}})";
    Reply reply;
    for (size_t cut = 0; cut < strlen(full); cut++) {
        TEST_ASSERT_EQUAL(ReplyKind::Ignore, parseReply(full, cut, 5, reply));
    }
    TEST_ASSERT_EQUAL(ReplyKind::Terminal, parseReply(full, strlen(full), 5, reply));
    TEST_ASSERT_EQUAL_STRING(R"({"state":"menu.idle"})", reply.resultJson.c_str());
}

void test_parses_unterminated_buffer_by_length() {
    // BLE data is not NUL-terminated.
    const char text[] = R"({"v":1,"id":9,"stage":"completed","ok":true})";
    char buffer[sizeof(text) + 8];
    memcpy(buffer, text, sizeof(text) - 1);
    memset(buffer + sizeof(text) - 1, 'x', 9);
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Terminal,
                      parseReply(buffer, sizeof(text) - 1, 9, reply));
    TEST_ASSERT_TRUE(reply.ok);
}

void test_ignores_null_empty_and_zero_id() {
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Ignore, parseReply(nullptr, 10, 1, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore, parseReply("{}", 0, 1, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"v":1,"id":0,"stage":"completed","ok":true})", 0, reply));
}

void test_ignores_ids_of_the_wrong_type() {
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"v":1,"id":"5","stage":"completed","ok":true})", 5, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"v":1,"id":-5,"stage":"completed","ok":true})", 5, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"v":1,"id":5.5,"stage":"completed","ok":true})", 5, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"v":1,"id":4294967301,"stage":"completed","ok":true})", 5, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore, parse(R"([5,"completed"])", 5, reply));
}

void test_ok_must_be_true_and_completed() {
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Terminal,
                      parse(R"({"v":1,"id":5,"stage":"completed","ok":"true"})", 5, reply));
    TEST_ASSERT_FALSE(reply.ok);
    TEST_ASSERT_EQUAL(ReplyKind::Terminal,
                      parse(R"({"v":1,"id":5,"stage":"failed","ok":true})", 5, reply));
    TEST_ASSERT_FALSE(reply.ok);
    TEST_ASSERT_EQUAL(ReplyKind::Terminal,
                      parse(R"({"v":1,"id":5,"stage":"completed"})", 5, reply));
    TEST_ASSERT_FALSE(reply.ok);
    TEST_ASSERT_EQUAL_STRING("failed", reply.code.c_str());
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"v":1,"id":5,"stage":"running","ok":true})", 5, reply));
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parse(R"({"v":1,"id":5,"stage":7,"ok":true})", 5, reply));
}

void test_oversized_reply_keeps_its_result() {
    std::string big(4000, 'a');
    const std::string json = R"({"v":1,"id":2,"stage":"completed","ok":true,"result":{"text":")" +
                             big + R"("}})";
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Terminal, parseReply(json.data(), json.size(), 2, reply));
    TEST_ASSERT_TRUE(reply.ok);
    TEST_ASSERT_EQUAL(big.size() + 11, reply.resultJson.size());
}

void test_deeply_nested_reply_is_ignored() {
    // ArduinoJson stops at its nesting limit instead of recursing on.
    std::string json = R"({"v":1,"id":3,"stage":"completed","ok":true,"result":)";
    for (int i = 0; i < 200; i++) json += "[";
    for (int i = 0; i < 200; i++) json += "]";
    json += "}";
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Ignore, parseReply(json.data(), json.size(), 3, reply));
}

void test_binary_garbage_is_ignored() {
    const unsigned char garbage[] = {0x7b, 0x00, 0xff, 0xfe, 0x22, 0x69, 0x64,
                                     0x22, 0x3a, 0x35, 0x7d, 0x80, 0x81};
    Reply reply;
    TEST_ASSERT_EQUAL(ReplyKind::Ignore,
                      parseReply(reinterpret_cast<const char *>(garbage),
                                 sizeof(garbage), 5, reply));
}

void test_lease_and_setting_reads_reject_wrong_types() {
    uint32_t lease = 77;
    TEST_ASSERT_FALSE(parseLease(R"({"lease":0})", lease));
    TEST_ASSERT_FALSE(parseLease(R"({"lease":"12"})", lease));
    TEST_ASSERT_FALSE(parseLease(R"({"lease":-3})", lease));
    TEST_ASSERT_FALSE(parseLease(R"({"lease":4294967296})", lease));
    TEST_ASSERT_FALSE(parseLease(R"({"lease":12)", lease));
    TEST_ASSERT_FALSE(parseLease("", lease));
    SettingRead read;
    TEST_ASSERT_FALSE(parseSettingRead(R"({"value":"300"})", read));
    TEST_ASSERT_FALSE(parseSettingRead(R"({"active":3})", read));
    TEST_ASSERT_FALSE(parseSettingRead(R"({"value":)", read));
}

void test_request_escapes_hostile_paths() {
    const std::string request =
        buildRequest(11, "setting.read", "a\"b\\c\n", "", 0);
    JsonDocument parsed;
    TEST_ASSERT_TRUE(deserializeJson(parsed, request) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("a\"b\\c\n", parsed["path"].as<const char *>());
    TEST_ASSERT_EQUAL(11, parsed["id"].as<int>());
}

}  // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_builds_read_request_without_args_or_lease);
    RUN_TEST(test_builds_write_request_with_args_and_lease);
    RUN_TEST(test_builds_acquire_request_without_path);
    RUN_TEST(test_ignores_reply_for_another_id);
    RUN_TEST(test_ignores_non_json_and_missing_id);
    RUN_TEST(test_skips_accepted_stage);
    RUN_TEST(test_parses_completed_setting_read);
    RUN_TEST(test_setting_read_without_active_uses_value);
    RUN_TEST(test_parses_failure_code_and_message);
    RUN_TEST(test_unknown_path_and_timeout_mean_old_firmware);
    RUN_TEST(test_parses_lease_token);
    RUN_TEST(test_default_geometry_is_valid);
    RUN_TEST(test_geometry_errors_match_ossm_messages);
    RUN_TEST(test_encoder_round_trip_uses_field_precision);
    RUN_TEST(test_formats_labels_with_units);
    RUN_TEST(test_plan_writes_skips_unchanged_fields);
    RUN_TEST(test_plan_writes_orders_dependent_fields);
    RUN_TEST(test_plan_writes_rejects_invalid_target);
    RUN_TEST(test_pause_first_falls_back_to_builtin_font);
    RUN_TEST(test_short_labels_keep_button_font);
    RUN_TEST(test_builtin_text_is_vertically_centred);
    RUN_TEST(test_ignores_truncated_reply);
    RUN_TEST(test_parses_unterminated_buffer_by_length);
    RUN_TEST(test_ignores_null_empty_and_zero_id);
    RUN_TEST(test_ignores_ids_of_the_wrong_type);
    RUN_TEST(test_ok_must_be_true_and_completed);
    RUN_TEST(test_oversized_reply_keeps_its_result);
    RUN_TEST(test_deeply_nested_reply_is_ignored);
    RUN_TEST(test_binary_garbage_is_ignored);
    RUN_TEST(test_lease_and_setting_reads_reject_wrong_types);
    RUN_TEST(test_request_escapes_hostile_paths);
    return UNITY_END();
}

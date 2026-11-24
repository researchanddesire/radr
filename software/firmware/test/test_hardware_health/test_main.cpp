#include <unity.h>

#include <cstring>

#include "services/hardwareHealthFormat.h"

using namespace hardwareHealth;

void setUp() {}
void tearDown() {}

void test_boot_record_is_one_prefixed_json_line() {
    char deviceId[13];
    formatDeviceId(0x0000665544332211ULL, deviceId);
    uint8_t digest[32];
    for (size_t i = 0; i < sizeof(digest); ++i) digest[i] = static_cast<uint8_t>((i % 16) * 17);
    char imageSha256[65];
    formatSha256(digest, imageSha256);
    char record[RECORD_CAPACITY];
    const size_t size = formatBoot(
        record, sizeof(record),
        {4294967295UL, 1234ULL, "0123abcd", "main", "r8", deviceId, imageSha256, 1});
    TEST_ASSERT_EQUAL_STRING(
        "\nRADR_HEALTH {\"event\":\"boot\",\"boot_id\":4294967295,\"uptime_ms\":1234,"
        "\"build_sha\":\"0123abcd\",\"track\":\"main\",\"variant\":\"r8\","
        "\"device_id\":\"112233445566\",\"image_sha256\":"
        "\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\","
        "\"reset_reason\":1}\n",
        record);
    TEST_ASSERT_EQUAL(std::strlen(record), size);
}

void test_device_id_matches_serial_identity_order() {
    char deviceId[13];
    formatDeviceId(0xFFFF060504030201ULL, deviceId);
    TEST_ASSERT_EQUAL_STRING("010203040506", deviceId);
}

void test_missing_image_hash_prints_empty_string() {
    char imageSha256[65];
    formatSha256(nullptr, imageSha256);
    TEST_ASSERT_EQUAL_STRING("", imageSha256);
}

void test_boot_refuses_values_that_need_escaping() {
    char record[RECORD_CAPACITY];
    TEST_ASSERT_EQUAL(0, formatBoot(record, sizeof(record),
                                    {1, 1, "bad\"sha", "main", "r8", "0", "", 0}));
    TEST_ASSERT_EQUAL(0, formatBoot(record, sizeof(record),
                                    {1, 1, "sha", "main\n", "r8", "0", "", 0}));
    TEST_ASSERT_EQUAL(0, formatBoot(record, sizeof(record),
                                    {1, 1, nullptr, "main", "r8", "0", "", 0}));
}

void test_wifi_record_prints_lwip_ipv4_in_octet_order() {
    char record[160];
    // lwIP keeps the first octet in the lowest byte.
    const size_t size = formatWifiConnected(record, sizeof(record), 7,
                                            18446744073709551615ULL, 0x1401A8C0UL);
    TEST_ASSERT_EQUAL_STRING(
        "\nRADR_HEALTH {\"event\":\"wifi_connected\",\"boot_id\":7,"
        "\"uptime_ms\":18446744073709551615,\"ip\":\"192.168.1.20\"}\n",
        record);
    TEST_ASSERT_EQUAL(std::strlen(record), size);
}

void test_heartbeat_reports_menu_and_internal_ram() {
    char record[224];
    const size_t size =
        formatHeartbeat(record, sizeof(record), {9, 60000, true, 16, 31744, 16384});
    TEST_ASSERT_EQUAL_STRING(
        "\nRADR_HEALTH {\"event\":\"heartbeat\",\"boot_id\":9,\"uptime_ms\":60000,"
        "\"main_menu\":true,\"menu_age_ms\":16,\"ram_free\":31744,"
        "\"ram_largest\":16384}\n",
        record);
    TEST_ASSERT_EQUAL(std::strlen(record), size);
    formatHeartbeat(record, sizeof(record), {9, 60000, false, 4294967295UL, 0, 0});
    TEST_ASSERT_NOT_NULL(std::strstr(record, "\"main_menu\":false,\"menu_age_ms\":4294967295,"));
}

void test_longest_heartbeat_fits_its_buffer() {
    char record[224];
    TEST_ASSERT_NOT_EQUAL(
        0, formatHeartbeat(record, sizeof(record),
                           {4294967295UL, 18446744073709551615ULL, false, 4294967295UL,
                            4294967295UL, 4294967295UL}));
}

void test_record_that_does_not_fit_is_dropped() {
    char record[40];
    TEST_ASSERT_EQUAL(0, formatHeartbeat(record, sizeof(record), {1, 1, true, 1, 1, 1}));
    TEST_ASSERT_EQUAL(0, formatWifiConnected(record, sizeof(record), 1, 1, 0));
}

void test_heartbeat_is_due_every_five_seconds() {
    uint32_t last = 0;
    TEST_ASSERT_FALSE(heartbeatDue(4999, last));
    TEST_ASSERT_TRUE(heartbeatDue(5000, last));
    TEST_ASSERT_EQUAL(5000, last);
    TEST_ASSERT_FALSE(heartbeatDue(9999, last));
    TEST_ASSERT_TRUE(heartbeatDue(10001, last));
    // millis() wraps after 49.7 days; the interval still holds.
    last = 4294966000UL;
    TEST_ASSERT_FALSE(heartbeatDue(1000, last));
    TEST_ASSERT_TRUE(heartbeatDue(3704, last));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_record_is_one_prefixed_json_line);
    RUN_TEST(test_device_id_matches_serial_identity_order);
    RUN_TEST(test_missing_image_hash_prints_empty_string);
    RUN_TEST(test_boot_refuses_values_that_need_escaping);
    RUN_TEST(test_wifi_record_prints_lwip_ipv4_in_octet_order);
    RUN_TEST(test_heartbeat_reports_menu_and_internal_ram);
    RUN_TEST(test_longest_heartbeat_fits_its_buffer);
    RUN_TEST(test_record_that_does_not_fit_is_dropped);
    RUN_TEST(test_heartbeat_is_due_every_five_seconds);
    return UNITY_END();
}

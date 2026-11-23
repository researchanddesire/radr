#include <unity.h>

#include <string>

#include "ossm_mode_follow_logic.h"

using namespace ossm_mode_follow;

namespace {

void test_ready_when_ossm_is_in_the_mode() {
    TEST_ASSERT_TRUE(next("strokeEngine.idle", "strokeEngine", 0) == Step::Ready);
    TEST_ASSERT_TRUE(next("strokeEngine.pattern", "strokeEngine", 3) == Step::Ready);
    TEST_ASSERT_TRUE(next("strokeEngine.preflight", "strokeEngine", 0) == Step::Ready);
    TEST_ASSERT_TRUE(next("simplePenetration.idle", "simplePenetration", 0) == Step::Ready);
    TEST_ASSERT_TRUE(next("streaming.idle", "streaming", 0) == Step::Ready);
}

void test_waits_while_the_ossm_homes() {
    TEST_ASSERT_TRUE(next("homing", "strokeEngine", 0) == Step::WaitForHoming);
    TEST_ASSERT_TRUE(next("homing.forward", "strokeEngine", 0) == Step::WaitForHoming);
    TEST_ASSERT_TRUE(next("homing.backward", "simplePenetration", 0) == Step::WaitForHoming);
    TEST_ASSERT_TRUE(next("idle", "strokeEngine", 0) == Step::WaitForHoming);
    // Waiting does not use up requests.
    TEST_ASSERT_TRUE(next("homing.forward", "strokeEngine", kMaxRequests) == Step::WaitForHoming);
}

void test_requests_the_mode_from_the_menu_after_homing() {
    TEST_ASSERT_TRUE(next("menu.idle", "strokeEngine", 0) == Step::Request);
    TEST_ASSERT_TRUE(next("menu", "simplePenetration", 1) == Step::Request);
}

void test_gives_up_after_the_request_limit() {
    TEST_ASSERT_TRUE(next("menu.idle", "strokeEngine", kMaxRequests - 1) == Step::Request);
    TEST_ASSERT_TRUE(next("menu.idle", "strokeEngine", kMaxRequests) == Step::GiveUp);
}

void test_never_presses_through_an_error() {
    TEST_ASSERT_TRUE(next("error", "strokeEngine", 0) == Step::GiveUp);
    TEST_ASSERT_TRUE(next("error.idle", "strokeEngine", 0) == Step::GiveUp);
    TEST_ASSERT_TRUE(next("error.help", "strokeEngine", 0) == Step::GiveUp);
}

void test_stops_when_no_mode_is_wanted() {
    TEST_ASSERT_TRUE(next("homing.forward", "", 0) == Step::GiveUp);
    TEST_ASSERT_TRUE(next("menu.idle", "", 0) == Step::GiveUp);
}

void test_an_unreadable_state_is_requested_like_a_menu() {
    TEST_ASSERT_TRUE(next("", "strokeEngine", 0) == Step::Request);
    TEST_ASSERT_TRUE(next("unknown", "strokeEngine", 0) == Step::Request);
}

void test_parses_flat_state_read_result() {
    std::string state;
    TEST_ASSERT_TRUE(parseStateName(R"({"state":"homing.forward"})", state));
    TEST_ASSERT_EQUAL_STRING("homing.forward", state.c_str());
}

void test_parses_nested_state_read_result() {
    std::string state;
    TEST_ASSERT_TRUE(parseStateName(R"({"state":{"state":"menu.idle","v":1,"sequence":3}})", state));
    TEST_ASSERT_EQUAL_STRING("menu.idle", state.c_str());
}

void test_rejects_results_without_a_state() {
    std::string state;
    TEST_ASSERT_FALSE(parseStateName("", state));
    TEST_ASSERT_FALSE(parseStateName("not json", state));
    TEST_ASSERT_FALSE(parseStateName(R"({"value":1})", state));
    TEST_ASSERT_FALSE(parseStateName(R"({"state":""})", state));
    TEST_ASSERT_FALSE(parseStateName(R"({"state":{"name":"menu"}})", state));
}

}  // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ready_when_ossm_is_in_the_mode);
    RUN_TEST(test_waits_while_the_ossm_homes);
    RUN_TEST(test_requests_the_mode_from_the_menu_after_homing);
    RUN_TEST(test_gives_up_after_the_request_limit);
    RUN_TEST(test_never_presses_through_an_error);
    RUN_TEST(test_stops_when_no_mode_is_wanted);
    RUN_TEST(test_an_unreadable_state_is_requested_like_a_menu);
    RUN_TEST(test_parses_flat_state_read_result);
    RUN_TEST(test_parses_nested_state_read_result);
    RUN_TEST(test_rejects_results_without_a_state);
    return UNITY_END();
}

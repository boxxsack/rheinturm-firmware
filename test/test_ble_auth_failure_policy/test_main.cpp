// Host-side unit test for BleAuthFailurePolicy (BLE bond self-heal). Run with
// `pio test -e native`.
//
// Covers the decision logic only: which auth-complete outcomes lead the
// firmware to remove the ESP32's bond and disconnect. The bluedroid calls that
// carry out the decision (esp_ble_remove_bond_device, esp_ble_gap_disconnect)
// can only be exercised on a real device with a real phone.
#include <unity.h>

#include <cstring>

#include "BleAuthFailurePolicy.h"

using BleAuthFailurePolicy::Action;
using BleAuthFailurePolicy::decide;
using BleAuthFailurePolicy::describeFailReason;
using BleAuthFailurePolicy::kSmpFailReasonBase;

void test_success_never_touches_bonds(void) {
    TEST_ASSERT_TRUE(decide(true, 0) == Action::None);
    // fail_reason is meaningless on success; a stray value must not trigger removal.
    TEST_ASSERT_TRUE(decide(true, BleAuthFailurePolicy::kFailSmpEncFail) == Action::None);
}

void test_stale_key_encryption_failure_removes_bond(void) {
    // A peer encrypting with a long-term key that does not match ours surfaces as SMP_ENC_FAIL.
    TEST_ASSERT_TRUE(decide(false, BleAuthFailurePolicy::kFailSmpEncFail) == Action::RemoveBondAndDisconnect);
}

void test_pairing_failures_remove_bond(void) {
    const uint8_t smpReasons[] = {0x01, 0x03, 0x04, 0x05, 0x08, 0x0B, 0x16, 0x18};
    for (uint8_t smp : smpReasons) {
        TEST_ASSERT_TRUE_MESSAGE(decide(false, kSmpFailReasonBase + smp) == Action::RemoveBondAndDisconnect,
                                 describeFailReason(kSmpFailReasonBase + smp));
    }
}

void test_unrecognised_failure_code_still_removes_bond(void) {
    // Fail safe towards recovery: an unknown failure must not leave a stale bond in place.
    TEST_ASSERT_TRUE(decide(false, 0x00) == Action::RemoveBondAndDisconnect);
    TEST_ASSERT_TRUE(decide(false, 0xFF) == Action::RemoveBondAndDisconnect);
}

void test_link_loss_does_not_remove_bond(void) {
    TEST_ASSERT_TRUE(decide(false, BleAuthFailurePolicy::kFailSmpConnTimeout) == Action::None);
}

void test_concurrent_pairing_does_not_remove_bond(void) {
    TEST_ASSERT_TRUE(decide(false, BleAuthFailurePolicy::kFailSmpBusy) == Action::None);
}

void test_fail_reason_codes_match_bluedroid(void) {
    // BTA_DM_AUTH_FAIL_BASE = HCI_ERR_MAX_ERR (0x43) + 10.
    TEST_ASSERT_EQUAL_HEX8(0x4D, kSmpFailReasonBase);
    TEST_ASSERT_EQUAL_HEX8(0x61, BleAuthFailurePolicy::kFailSmpEncFail);
    TEST_ASSERT_EQUAL_HEX8(0x60, BleAuthFailurePolicy::kFailSmpBusy);
    TEST_ASSERT_EQUAL_HEX8(0x66, BleAuthFailurePolicy::kFailSmpConnTimeout);
}

void test_describe_fail_reason(void) {
    TEST_ASSERT_EQUAL_STRING("SMP_ENC_FAIL", describeFailReason(BleAuthFailurePolicy::kFailSmpEncFail));
    TEST_ASSERT_EQUAL_STRING("SMP_CONN_TOUT", describeFailReason(BleAuthFailurePolicy::kFailSmpConnTimeout));
    TEST_ASSERT_EQUAL_STRING("SMP_PAIR_NOT_SUPPORT", describeFailReason(kSmpFailReasonBase + 0x05));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", describeFailReason(0x10));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", describeFailReason(kSmpFailReasonBase + 0x1A));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_success_never_touches_bonds);
    RUN_TEST(test_stale_key_encryption_failure_removes_bond);
    RUN_TEST(test_pairing_failures_remove_bond);
    RUN_TEST(test_unrecognised_failure_code_still_removes_bond);
    RUN_TEST(test_link_loss_does_not_remove_bond);
    RUN_TEST(test_concurrent_pairing_does_not_remove_bond);
    RUN_TEST(test_fail_reason_codes_match_bluedroid);
    RUN_TEST(test_describe_fail_reason);
    return UNITY_END();
}

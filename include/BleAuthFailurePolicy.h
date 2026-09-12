#pragma once

#include <cstdint>

// Decides how the firmware reacts to a BLE authentication-complete event
// (ESP_GAP_BLE_AUTH_CMPL_EVT). Fixes the asymmetric-bond case where the ESP32
// holds a stale or half-written bond: on failure the ESP32 drops its side of
// the bond and disconnects, so the next connection pairs from scratch instead
// of leaving encrypted characteristics stuck at "Writing is not permitted".
//
// It cannot fix the reverse case (iOS holds a stale key, the ESP32 has none):
// there is nothing on the ESP32 to remove, and the user must forget the device
// in iOS Settings.
//
// Pure logic with no Arduino/ESP-IDF dependency, so it is testable on the host -
// see test/test_ble_auth_failure_policy.
namespace BleAuthFailurePolicy {

// For BLE, bluedroid reports esp_ble_auth_cmpl_t::fail_reason as
// BTA_DM_AUTH_CONVERT_SMP_CODE(smp_reason) = (HCI_ERR_MAX_ERR + 10) + smp_reason,
// i.e. 0x4D + the SMP status code (esp-idf release/v4.4:
// bta/include/bta/bta_api.h, stack/include/stack/smp_api.h).
constexpr uint8_t kSmpFailReasonBase = 0x4D;
constexpr uint8_t kFailSmpEncFail = kSmpFailReasonBase + 0x14;   // encryption with stored key failed
constexpr uint8_t kFailSmpBusy = kSmpFailReasonBase + 0x13;      // another pairing already in progress
constexpr uint8_t kFailSmpConnTimeout = kSmpFailReasonBase + 0x19;  // link dropped during security procedure

enum class Action : uint8_t {
    None,                    // leave bonds and the link alone
    RemoveBondAndDisconnect  // drop the ESP32's bond for this peer and disconnect it
};

// success     esp_ble_auth_cmpl_t::success
// failReason  esp_ble_auth_cmpl_t::fail_reason (ignored when success is true)
//
// Success never touches bonds. A failure removes the bond and disconnects,
// except when the failure says nothing about the bond itself: the link is
// already gone (SMP connection timeout) or a concurrent pairing on the same
// link is still running (SMP busy) and will store fresh keys on its own.
Action decide(bool success, uint8_t failReason);

// Human-readable name of a fail_reason for serial diagnostics; never null.
const char* describeFailReason(uint8_t failReason);

}  // namespace BleAuthFailurePolicy

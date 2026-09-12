#include "BleAuthFailurePolicy.h"

namespace BleAuthFailurePolicy {

Action decide(bool success, uint8_t failReason) {
    if (success) {
        return Action::None;
    }
    if (failReason == kFailSmpConnTimeout || failReason == kFailSmpBusy) {
        return Action::None;
    }
    return Action::RemoveBondAndDisconnect;
}

const char* describeFailReason(uint8_t failReason) {
    // SMP status names indexed by (failReason - kSmpFailReasonBase), per
    // esp-idf stack/include/stack/smp_api.h.
    static const char* const kSmpNames[] = {
        "SMP_SUCCESS",
        "SMP_PASSKEY_ENTRY_FAIL",
        "SMP_OOB_FAIL",
        "SMP_PAIR_AUTH_FAIL",
        "SMP_CONFIRM_VALUE_ERR",
        "SMP_PAIR_NOT_SUPPORT",
        "SMP_ENC_KEY_SIZE",
        "SMP_INVALID_CMD",
        "SMP_PAIR_FAIL_UNKNOWN",
        "SMP_REPEATED_ATTEMPTS",
        "SMP_INVALID_PARAMETERS",
        "SMP_DHKEY_CHK_FAIL",
        "SMP_NUMERIC_COMPAR_FAIL",
        "SMP_BR_PAIRING_IN_PROGR",
        "SMP_XTRANS_DERIVE_NOT_ALLOW",
        "SMP_PAIR_INTERNAL_ERR",
        "SMP_UNKNOWN_IO_CAP",
        "SMP_INIT_FAIL",
        "SMP_CONFIRM_FAIL",
        "SMP_BUSY",
        "SMP_ENC_FAIL",
        "SMP_STARTED",
        "SMP_RSP_TIMEOUT",
        "SMP_DIV_NOT_AVAIL",
        "SMP_FAIL",
        "SMP_CONN_TOUT",
    };
    const unsigned count = sizeof(kSmpNames) / sizeof(kSmpNames[0]);
    if (failReason >= kSmpFailReasonBase && static_cast<unsigned>(failReason - kSmpFailReasonBase) < count) {
        return kSmpNames[failReason - kSmpFailReasonBase];
    }
    return "UNKNOWN";
}

}  // namespace BleAuthFailurePolicy

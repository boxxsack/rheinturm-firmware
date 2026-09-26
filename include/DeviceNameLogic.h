#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace DeviceNameLogic {

constexpr const char* kDefaultName = "Rheinturm";
constexpr size_t kMinCharacters = 1;
constexpr size_t kMaxCharacters = 20;
// BLE AD structures reserve one byte for their length and one for their type.
// The name is the only field in the scan response, so 29 bytes is the complete
// local-name payload that fits in the 31-byte legacy scan-response PDU.
constexpr size_t kMaxEncodedBytes = 29;

enum class ValidationError : uint8_t {
    None,
    Empty,
    InvalidUtf8,
    InvalidCharacter,
    TooManyCharacters,
    TooManyBytes,
};

// Validates a raw BLE write and returns its trimmed, canonical form. Leading
// and trailing ASCII spaces are removed before character and byte limits are
// checked. The input is not required to be NUL-terminated.
bool validateAndNormalize(const uint8_t* input,
                          size_t length,
                          std::string& normalized,
                          ValidationError& error);

const char* errorCode(ValidationError error);

} // namespace DeviceNameLogic

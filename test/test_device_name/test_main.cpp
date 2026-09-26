#include <unity.h>

#include <cstdint>
#include <string>

#include "DeviceNameLogic.h"

using DeviceNameLogic::ValidationError;

namespace {

bool validate(const std::string& input, std::string& normalized, ValidationError& error) {
    return DeviceNameLogic::validateAndNormalize(
        reinterpret_cast<const uint8_t*>(input.data()), input.size(), normalized, error);
}

void test_names_are_trimmed_and_latin_letters_are_accepted(void) {
    std::string normalized;
    ValidationError error;
    TEST_ASSERT_TRUE(validate("  Düssel-Turm  ", normalized, error));
    TEST_ASSERT_EQUAL_STRING("Düssel-Turm", normalized.c_str());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ValidationError::None),
                            static_cast<uint8_t>(error));
}

void test_character_limit_counts_code_points_not_utf8_bytes(void) {
    std::string normalized;
    ValidationError error;
    TEST_ASSERT_TRUE(validate("Ä1234567890123456789", normalized, error));
    TEST_ASSERT_EQUAL_STRING("Ä1234567890123456789", normalized.c_str());

    TEST_ASSERT_FALSE(validate("123456789012345678901", normalized, error));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ValidationError::TooManyCharacters),
                            static_cast<uint8_t>(error));
}

void test_scan_response_limit_counts_encoded_bytes(void) {
    std::string normalized;
    ValidationError error;
    TEST_ASSERT_TRUE(validate("ÄÄÄÄÄÄÄÄÄÄÄÄÄÄA", normalized, error)); // 29 bytes
    TEST_ASSERT_FALSE(validate("ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ", normalized, error)); // 30 bytes
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ValidationError::TooManyBytes),
                            static_cast<uint8_t>(error));
}

void test_invalid_utf8_and_characters_are_rejected(void) {
    std::string normalized;
    ValidationError error;
    const std::string invalidUtf8("Tower\xC3\x28", 7);
    TEST_ASSERT_FALSE(validate(invalidUtf8, normalized, error));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ValidationError::InvalidUtf8),
                            static_cast<uint8_t>(error));

    TEST_ASSERT_FALSE(validate("Tower_1", normalized, error));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ValidationError::InvalidCharacter),
                            static_cast<uint8_t>(error));
}

void test_empty_or_whitespace_names_are_rejected(void) {
    std::string normalized;
    ValidationError error;
    TEST_ASSERT_FALSE(validate("   ", normalized, error));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ValidationError::Empty),
                            static_cast<uint8_t>(error));
    TEST_ASSERT_FALSE(validate("", normalized, error));
    TEST_ASSERT_EQUAL_STRING("empty", DeviceNameLogic::errorCode(error));
}

} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_names_are_trimmed_and_latin_letters_are_accepted);
    RUN_TEST(test_character_limit_counts_code_points_not_utf8_bytes);
    RUN_TEST(test_scan_response_limit_counts_encoded_bytes);
    RUN_TEST(test_invalid_utf8_and_characters_are_rejected);
    RUN_TEST(test_empty_or_whitespace_names_are_rejected);
    return UNITY_END();
}

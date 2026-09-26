#include "DeviceNameLogic.h"

namespace DeviceNameLogic {
namespace {

bool isContinuation(uint8_t value) {
    return (value & 0xC0u) == 0x80u;
}

bool decodeCodePoint(const uint8_t* input,
                     size_t length,
                     size_t& offset,
                     uint32_t& codePoint) {
    if (offset >= length) return false;

    const uint8_t first = input[offset++];
    if (first <= 0x7Fu) {
        codePoint = first;
        return true;
    }

    if (first >= 0xC2u && first <= 0xDFu) {
        if (offset >= length || !isContinuation(input[offset])) return false;
        codePoint = (static_cast<uint32_t>(first & 0x1Fu) << 6)
            | (input[offset++] & 0x3Fu);
        return true;
    }

    if (first >= 0xE0u && first <= 0xEFu) {
        if (offset + 1 >= length || !isContinuation(input[offset])
            || !isContinuation(input[offset + 1])) {
            return false;
        }
        const uint8_t second = input[offset];
        if ((first == 0xE0u && second < 0xA0u)
            || (first == 0xEDu && second > 0x9Fu)) {
            return false;
        }
        codePoint = (static_cast<uint32_t>(first & 0x0Fu) << 12)
            | (static_cast<uint32_t>(second & 0x3Fu) << 6)
            | (input[offset + 1] & 0x3Fu);
        offset += 2;
        return true;
    }

    if (first >= 0xF0u && first <= 0xF4u) {
        if (offset + 2 >= length || !isContinuation(input[offset])
            || !isContinuation(input[offset + 1])
            || !isContinuation(input[offset + 2])) {
            return false;
        }
        const uint8_t second = input[offset];
        if ((first == 0xF0u && second < 0x90u)
            || (first == 0xF4u && second > 0x8Fu)) {
            return false;
        }
        codePoint = (static_cast<uint32_t>(first & 0x07u) << 18)
            | (static_cast<uint32_t>(second & 0x3Fu) << 12)
            | (static_cast<uint32_t>(input[offset + 1] & 0x3Fu) << 6)
            | (input[offset + 2] & 0x3Fu);
        offset += 3;
        return true;
    }

    return false;
}

bool isLatinLetter(uint32_t codePoint) {
    return (codePoint >= 'A' && codePoint <= 'Z')
        || (codePoint >= 'a' && codePoint <= 'z')
        || (codePoint >= 0x00C0u && codePoint <= 0x00D6u)
        || (codePoint >= 0x00D8u && codePoint <= 0x00F6u)
        || (codePoint >= 0x00F8u && codePoint <= 0x00FFu)
        || (codePoint >= 0x0100u && codePoint <= 0x017Fu)
        || (codePoint >= 0x0180u && codePoint <= 0x024Fu)
        || (codePoint >= 0x1E00u && codePoint <= 0x1EFFu);
}

bool isAllowed(uint32_t codePoint) {
    return isLatinLetter(codePoint)
        || (codePoint >= '0' && codePoint <= '9')
        || codePoint == ' '
        || codePoint == '-';
}

} // namespace

bool validateAndNormalize(const uint8_t* input,
                          size_t length,
                          std::string& normalized,
                          ValidationError& error) {
    normalized.clear();
    error = ValidationError::None;
    if (input == nullptr && length != 0) {
        error = ValidationError::InvalidUtf8;
        return false;
    }

    bool started = false;
    size_t characterCount = 0;
    size_t trailingSpaces = 0;
    size_t offset = 0;
    while (offset < length) {
        const size_t codePointStart = offset;
        uint32_t codePoint = 0;
        if (!decodeCodePoint(input, length, offset, codePoint)) {
            error = ValidationError::InvalidUtf8;
            return false;
        }
        if (!isAllowed(codePoint)) {
            error = ValidationError::InvalidCharacter;
            return false;
        }
        if (!started && codePoint == ' ') continue;

        started = true;
        normalized.append(reinterpret_cast<const char*>(input + codePointStart),
                          offset - codePointStart);
        ++characterCount;
        if (codePoint == ' ') {
            ++trailingSpaces;
        } else {
            trailingSpaces = 0;
        }
    }

    if (trailingSpaces > 0) {
        normalized.resize(normalized.size() - trailingSpaces);
        characterCount -= trailingSpaces;
    }
    if (characterCount < kMinCharacters) {
        error = ValidationError::Empty;
        return false;
    }
    if (characterCount > kMaxCharacters) {
        error = ValidationError::TooManyCharacters;
        return false;
    }
    if (normalized.size() > kMaxEncodedBytes) {
        error = ValidationError::TooManyBytes;
        return false;
    }
    return true;
}

const char* errorCode(ValidationError error) {
    switch (error) {
    case ValidationError::None: return "ok";
    case ValidationError::Empty: return "empty";
    case ValidationError::InvalidUtf8: return "invalid_utf8";
    case ValidationError::InvalidCharacter: return "invalid_character";
    case ValidationError::TooManyCharacters: return "too_many_characters";
    case ValidationError::TooManyBytes: return "too_many_bytes";
    }
    return "invalid";
}

} // namespace DeviceNameLogic

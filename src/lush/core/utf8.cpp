#include "lush/core/utf8.h"

namespace lush::core {
namespace {

bool isContinuation(unsigned char byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

bool isSurrogate(char32_t codePoint) noexcept {
    return codePoint >= 0xD800 && codePoint <= 0xDFFF;
}

} // namespace

std::optional<Utf8CodePoint> decodeUtf8(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return std::nullopt;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t size = text.size();
    const unsigned char first = bytes[offset];

    if (first < 0x80U) {
        return Utf8CodePoint{static_cast<char32_t>(first), 1};
    }

    if (first >= 0xC2U && first <= 0xDFU) {
        if (offset + 1 >= size || !isContinuation(bytes[offset + 1])) {
            return std::nullopt;
        }
        const char32_t value = (static_cast<char32_t>(first & 0x1FU) << 6) |
                               static_cast<char32_t>(bytes[offset + 1] & 0x3FU);
        return Utf8CodePoint{value, 2};
    }

    if (first >= 0xE0U && first <= 0xEFU) {
        if (offset + 2 >= size) {
            return std::nullopt;
        }
        const unsigned char second = bytes[offset + 1];
        const unsigned char third = bytes[offset + 2];
        if (!isContinuation(second) || !isContinuation(third)) {
            return std::nullopt;
        }
        if (first == 0xE0U && second < 0xA0U) {
            return std::nullopt;
        }
        if (first == 0xEDU && second > 0x9FU) {
            return std::nullopt;
        }
        const char32_t value = (static_cast<char32_t>(first & 0x0FU) << 12) |
                               (static_cast<char32_t>(second & 0x3FU) << 6) |
                               static_cast<char32_t>(third & 0x3FU);
        return Utf8CodePoint{value, 3};
    }

    if (first >= 0xF0U && first <= 0xF4U) {
        if (offset + 3 >= size) {
            return std::nullopt;
        }
        const unsigned char second = bytes[offset + 1];
        const unsigned char third = bytes[offset + 2];
        const unsigned char fourth = bytes[offset + 3];
        if (!isContinuation(second) || !isContinuation(third) || !isContinuation(fourth)) {
            return std::nullopt;
        }
        if (first == 0xF0U && second < 0x90U) {
            return std::nullopt;
        }
        if (first == 0xF4U && second > 0x8FU) {
            return std::nullopt;
        }
        const char32_t value = (static_cast<char32_t>(first & 0x07U) << 18) |
                               (static_cast<char32_t>(second & 0x3FU) << 12) |
                               (static_cast<char32_t>(third & 0x3FU) << 6) |
                               static_cast<char32_t>(fourth & 0x3FU);
        return Utf8CodePoint{value, 4};
    }

    return std::nullopt;
}

bool isValidUtf8(std::string_view text, std::size_t* invalidOffset) noexcept {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto decoded = decodeUtf8(text, offset);
        if (!decoded) {
            if (invalidOffset != nullptr) {
                *invalidOffset = offset;
            }
            return false;
        }
        offset += decoded->length;
    }

    if (invalidOffset != nullptr) {
        *invalidOffset = text.size();
    }
    return true;
}

std::size_t utf8Length(std::string_view text) noexcept {
    std::size_t length = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto decoded = decodeUtf8(text, offset);
        if (decoded) {
            offset += decoded->length;
        } else {
            ++offset;
        }
        ++length;
    }
    return length;
}

std::optional<std::string> utf8At(std::string_view text, std::size_t index) {
    std::size_t offset = 0;
    std::size_t current = 0;
    while (offset < text.size()) {
        const auto decoded = decodeUtf8(text, offset);
        if (!decoded) {
            return std::nullopt;
        }
        if (current == index) {
            return std::string(text.substr(offset, decoded->length));
        }
        offset += decoded->length;
        ++current;
    }
    return std::nullopt;
}

bool appendUtf8(std::string& out, char32_t codePoint) {
    if (codePoint > 0x10FFFFU || isSurrogate(codePoint)) {
        return false;
    }

    if (codePoint <= 0x7FU) {
        out.push_back(static_cast<char>(codePoint));
        return true;
    }

    if (codePoint <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        return true;
    }

    if (codePoint <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        return true;
    }

    out.push_back(static_cast<char>(0xF0U | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80U | ((codePoint >> 12) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((codePoint >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    return true;
}

} // namespace lush::core

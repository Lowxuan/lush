#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lush::core {

struct Utf8CodePoint {
    char32_t value = 0;
    std::size_t length = 0;
};

// Decodes one code point at offset. Returns nullopt for an invalid sequence.
std::optional<Utf8CodePoint> decodeUtf8(std::string_view text, std::size_t offset) noexcept;

// Validates the whole string. On failure, *invalidOffset is the first bad byte.
// On success, *invalidOffset is text.size().
bool isValidUtf8(std::string_view text, std::size_t* invalidOffset = nullptr) noexcept;

// Counts code points. Invalid bytes count as one unit each.
std::size_t utf8Length(std::string_view text) noexcept;

// Returns the index-th code point as a UTF-8 string, or nullopt.
std::optional<std::string> utf8At(std::string_view text, std::size_t index);

// Appends one code point. Rejects surrogates and values above U+10FFFF.
bool appendUtf8(std::string& out, char32_t codePoint);

} // namespace lush::core

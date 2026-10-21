#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lush::core {

struct Span {
    std::size_t start = 0;
    std::size_t end = 0;

    constexpr std::size_t length() const noexcept {
        return end - start;
    }

    constexpr bool empty() const noexcept {
        return start == end;
    }
};

struct LineCol {
    std::size_t line = 1;
    std::size_t column = 1;
};

// Owns the original source bytes and maps offsets to physical lines/columns.
// The text is never normalized; token offsets always point into text().
class Source {
public:
    explicit Source(std::string text);

    std::string_view text() const noexcept {
        return text_;
    }

    std::size_t size() const noexcept {
        return text_.size();
    }

    bool hasBom() const noexcept {
        return hasBom_;
    }

    std::size_t contentStart() const noexcept {
        return contentStart_;
    }

    std::string_view slice(Span span) const noexcept;

    std::size_t lineCount() const noexcept {
        return lineStarts_.size();
    }

    LineCol lineCol(std::size_t offset) const noexcept;

    // Both line numbers are 1-based. Returned text excludes BOM and newline.
    std::string_view lineText(std::size_t line) const noexcept;
    std::string_view lineTextAt(std::size_t offset) const noexcept;

private:
    std::string text_;
    bool hasBom_ = false;
    std::size_t contentStart_ = 0;
    std::vector<std::size_t> lineStarts_;
    std::vector<std::size_t> lineEnds_;
};

} // namespace lush::core

#include "lush/core/source.h"

#include "lush/core/utf8.h"

#include <algorithm>
#include <utility>

namespace lush::core {
namespace {

bool startsWithBom(std::string_view text) noexcept {
    return text.size() >= 3 &&
           static_cast<unsigned char>(text[0]) == 0xEFU &&
           static_cast<unsigned char>(text[1]) == 0xBBU &&
           static_cast<unsigned char>(text[2]) == 0xBFU;
}

} // namespace

Source::Source(std::string text) : text_(std::move(text)) {
    hasBom_ = startsWithBom(text_);
    contentStart_ = hasBom_ ? 3 : 0;

    lineStarts_.push_back(contentStart_);

    std::size_t offset = contentStart_;
    while (offset < text_.size()) {
        if (text_[offset] == '\r') {
            lineEnds_.push_back(offset);
            ++offset;
            if (offset < text_.size() && text_[offset] == '\n') {
                ++offset;
            }
            lineStarts_.push_back(offset);
        } else if (text_[offset] == '\n') {
            lineEnds_.push_back(offset);
            ++offset;
            lineStarts_.push_back(offset);
        } else {
            ++offset;
        }
    }
    lineEnds_.push_back(text_.size());
}

std::string_view Source::slice(Span span) const noexcept {
    std::size_t start = std::min(span.start, text_.size());
    std::size_t end = std::min(span.end, text_.size());
    if (end < start) {
        end = start;
    }
    return std::string_view(text_).substr(start, end - start);
}

LineCol Source::lineCol(std::size_t offset) const noexcept {
    if (offset < contentStart_) {
        offset = contentStart_;
    }
    if (offset > text_.size()) {
        offset = text_.size();
    }

    const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    std::size_t lineIndex = 0;
    if (it != lineStarts_.begin()) {
        lineIndex = static_cast<std::size_t>(it - lineStarts_.begin() - 1);
    }

    const std::size_t lineStart = lineStarts_[lineIndex];
    std::size_t current = lineStart;
    std::size_t column = 1;

    while (current < offset) {
        const auto decoded = decodeUtf8(text_, current);
        if (decoded) {
            current += decoded->length;
        } else {
            ++current;
        }
        ++column;
    }

    return LineCol{lineIndex + 1, column};
}

std::string_view Source::lineText(std::size_t line) const noexcept {
    if (line == 0 || line > lineStarts_.size()) {
        return {};
    }

    const std::size_t start = lineStarts_[line - 1];
    const std::size_t end = lineEnds_[line - 1];
    return std::string_view(text_).substr(start, end - start);
}

std::string_view Source::lineTextAt(std::size_t offset) const noexcept {
    if (offset < contentStart_) {
        offset = contentStart_;
    }
    if (offset > text_.size()) {
        offset = text_.size();
    }

    const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    std::size_t lineIndex = 0;
    if (it != lineStarts_.begin()) {
        lineIndex = static_cast<std::size_t>(it - lineStarts_.begin() - 1);
    }
    return lineText(lineIndex + 1);
}

} // namespace lush::core

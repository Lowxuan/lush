#include "lush/core/text.h"

#include <cstddef>

namespace lush::core {

char asciiLower(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

std::string asciiLower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (char character : text) {
        lowered.push_back(asciiLower(character));
    }
    return lowered;
}

bool asciiEqualsIgnoreCase(std::string_view left,
                           std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (asciiLower(left[index]) != asciiLower(right[index])) {
            return false;
        }
    }
    return true;
}

} // namespace lush::core

#pragma once

#include <cstdint>
#include <limits>

namespace lush::core {

inline bool checkedAdd(std::int64_t left, std::int64_t right,
                       std::int64_t& result) noexcept {
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();

    if ((right > 0 && left > max - right) ||
        (right < 0 && left < min - right)) {
        return false;
    }
    result = left + right;
    return true;
}

inline bool checkedSub(std::int64_t left, std::int64_t right,
                       std::int64_t& result) noexcept {
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();

    if ((right < 0 && left > max + right) ||
        (right > 0 && left < min + right)) {
        return false;
    }
    result = left - right;
    return true;
}

inline bool checkedMul(std::int64_t left, std::int64_t right,
                       std::int64_t& result) noexcept {
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();

    if (left == 0 || right == 0) {
        result = 0;
        return true;
    }

    if (left == -1) {
        if (right == min) {
            return false;
        }
        result = -right;
        return true;
    }

    if (right == -1) {
        if (left == min) {
            return false;
        }
        result = -left;
        return true;
    }

    if (left > 0) {
        if (right > 0) {
            if (left > max / right) {
                return false;
            }
        } else {
            if (right < min / left) {
                return false;
            }
        }
    } else {
        if (right > 0) {
            if (left < min / right) {
                return false;
            }
        } else {
            if (left < max / right) {
                return false;
            }
        }
    }

    result = left * right;
    return true;
}

inline bool checkedDiv(std::int64_t left, std::int64_t right,
                       std::int64_t& result) noexcept {
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();

    if (right == 0) {
        return false;
    }
    if (left == min && right == -1) {
        return false;
    }
    result = left / right;
    return true;
}

inline bool checkedMod(std::int64_t left, std::int64_t right,
                       std::int64_t& result) noexcept {
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();

    if (right == 0) {
        return false;
    }
    if (left == min && right == -1) {
        result = 0;
        return true;
    }
    result = left % right;
    return true;
}

inline bool checkedNegate(std::int64_t value, std::int64_t& result) noexcept {
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();

    if (value == min) {
        return false;
    }
    result = -value;
    return true;
}

} // namespace lush::core

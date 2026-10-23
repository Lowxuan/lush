#include "lush/core/format.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <system_error>

namespace lush::core {
namespace {

std::string formatInt(std::int64_t value) {
    char buffer[32]{};
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        return std::to_string(value);
    }
    return std::string(buffer, result.ptr);
}

std::string normalizeExponent(std::string text) {
    const std::size_t position = text.find_first_of("eE");
    if (position == std::string::npos) {
        return text;
    }

    const std::string mantissa = text.substr(0, position);
    std::string exponent = text.substr(position + 1);
    char sign = '+';
    std::size_t index = 0;

    if (index < exponent.size() &&
        (exponent[index] == '+' || exponent[index] == '-')) {
        sign = exponent[index];
        ++index;
    }

    while (index + 1 < exponent.size() && exponent[index] == '0') {
        ++index;
    }

    if (index >= exponent.size()) {
        exponent = "0";
    } else {
        exponent = exponent.substr(index);
    }

    return mantissa + "e" + sign + exponent;
}

std::string formatCallable(const Value& value) {
    const CallablePtr callable = value.asCallable();
    if (!callable) {
        return "callable";
    }
    return std::string(callable->name());
}

} // namespace

std::string formatDouble(double value) {
    if (value == 0.0) {
        return "0";
    }
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            return "nan";
        }
        return value < 0.0 ? "-inf" : "inf";
    }

    char buffer[64]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        return std::to_string(value);
    }

    return normalizeExponent(std::string(buffer, result.ptr));
}

std::string escapeString(std::string_view text) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(text.size());

    for (unsigned char byte : text) {
        switch (byte) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        case '\0': escaped += "\\0"; break;
        default:
            if (byte < 0x20U || byte == 0x7FU) {
                escaped += "\\u00";
                escaped.push_back(hex[(byte >> 4U) & 0x0FU]);
                escaped.push_back(hex[byte & 0x0FU]);
            } else {
                escaped.push_back(static_cast<char>(byte));
            }
            break;
        }
    }

    return escaped;
}

std::string formatValueNested(const Value& value) {
    switch (value.type()) {
    case ValueType::Int:
        return formatInt(value.asInt());
    case ValueType::Double:
        return formatDouble(value.asDouble());
    case ValueType::Bool:
        return value.asBool() ? "true" : "false";
    case ValueType::String:
        return "\"" + escapeString(value.asString()) + "\"";
    case ValueType::List: {
        std::string text = "[";
        const auto& items = value.asList().items();
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index != 0) {
                text += ", ";
            }
            text += formatValueNested(items[index]);
        }
        text += "]";
        return text;
    }
    case ValueType::Point: {
        const PointValue& point = value.asPoint();
        std::string text = "(";
        for (int index = 0; index < point.dim; ++index) {
            if (index != 0) {
                text += ", ";
            }
            text += formatDouble(point[index]);
        }
        text += ")";
        return text;
    }
    case ValueType::Callable:
        return formatCallable(value);
    }

    return "unknown";
}

std::string formatValue(const Value& value) {
    if (value.type() == ValueType::String) {
        return value.asString();
    }
    return formatValueNested(value);
}

} // namespace lush::core

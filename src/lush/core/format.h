#pragma once

#include "lush/core/value.h"

#include <string>
#include <string_view>

namespace lush::core {

// Top-level formatting: strings are raw (without quotes).
std::string formatValue(const Value& value);

// Nested formatting: strings are quoted and escaped.
std::string formatValueNested(const Value& value);

std::string formatDouble(double value);
std::string escapeString(std::string_view text);

} // namespace lush::core

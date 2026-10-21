#pragma once

#include <string_view>

#ifndef LUSH_VERSION
#define LUSH_VERSION "0.1.0"
#endif

namespace lush {

inline constexpr std::string_view kVersion = LUSH_VERSION;

std::string_view version() noexcept;

} // namespace lush

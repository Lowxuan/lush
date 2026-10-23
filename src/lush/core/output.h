#pragma once

#include <string_view>

namespace lush::core {

class Output {
public:
    virtual ~Output() = default;
    virtual void write(std::string_view text) = 0;
};

} // namespace lush::core

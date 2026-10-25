#pragma once

#include "lush/core/output.h"

namespace lush::cli {

class ConsoleSink final : public core::Output {
public:
    void write(std::string_view text) override;
};

} // namespace lush::cli

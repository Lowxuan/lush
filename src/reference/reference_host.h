#pragma once

#include "lush/command/registry.h"
#include "lush/core/output.h"

#include <istream>

namespace lush::reference {

class ReferenceHost {
public:
    ReferenceHost();
    ReferenceHost(std::istream& input, core::Output& output);

    command::Registry& registry() noexcept {
        return registry_;
    }

    const command::Registry& registry() const noexcept {
        return registry_;
    }

private:
    void initialize();

    command::Registry registry_;
    std::istream* input_ = nullptr;
    core::Output* output_ = nullptr;
};

} // namespace lush::reference

#pragma once

#include "lush/command/registry.h"

namespace lush::reference {

class ReferenceHost {
public:
    ReferenceHost();

    command::Registry& registry() noexcept {
        return registry_;
    }

    const command::Registry& registry() const noexcept {
        return registry_;
    }

private:
    command::Registry registry_;
};

} // namespace lush::reference

#pragma once

#include "lush/core/source.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace lush::driver {

// Owns the source texts referenced by SourceId within one Driver instance.
// Ids are monotonic and are never reused; kNoSource (0) is invalid.
class SourceTable {
public:
    core::SourceId add(std::string text);

    std::shared_ptr<const core::Source> find(core::SourceId id) const;

    void clear();

private:
    std::unordered_map<core::SourceId, std::shared_ptr<const core::Source>>
        sources_;
    core::SourceId next_ = 1;
};

} // namespace lush::driver

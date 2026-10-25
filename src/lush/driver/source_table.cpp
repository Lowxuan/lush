#include "lush/driver/source_table.h"

#include <utility>

namespace lush::driver {

core::SourceId SourceTable::add(std::string text) {
    const core::SourceId id = next_++;
    sources_.emplace(
        id, std::make_shared<const core::Source>(std::move(text)));
    return id;
}

std::shared_ptr<const core::Source> SourceTable::find(
    core::SourceId id) const {
    if (id == core::kNoSource) {
        return nullptr;
    }
    const auto found = sources_.find(id);
    if (found == sources_.end()) {
        return nullptr;
    }
    return found->second;
}

void SourceTable::clear() {
    sources_.clear();
}

} // namespace lush::driver

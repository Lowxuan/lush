#pragma once

#include "lush/core/source.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace lush::driver {

// Driver 生命周期内持有的源文本表。
//
// SourceId 单调递增且不复用；kNoSource (0) 无效。M8b 计划把该表上移到
// Env，使函数值持有的 Source 活到 Env 销毁。
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

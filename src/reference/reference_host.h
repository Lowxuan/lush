#pragma once

#include "lush/command/registry.h"
#include "lush/core/output.h"

#include <istream>

namespace lush::reference {

// CLI 演示宿主：注册 line/circle/move/read。
//
// Registry 归 ReferenceHost 所有；input/output 指针只在宿主生命周期内
// 有效，默认构造使用 std::cin/std::cout，测试可注入流。
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

#pragma once

#include "lush/core/builtins.h"
#include "lush/core/output.h"
#include "lush/core/value.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lush::command {

// 宿主 callable 的可显示错误；默认映射为 R010。
struct HostError {
    std::string message;
    std::optional<core::Span> span;
    core::SourceId source = core::kNoSource;
};

// 命令/值函数返回给求值器的统一结果：值、错误或两者皆空（void）。
struct CommandResult {
    std::optional<core::Value> value;
    std::optional<HostError> error;

    static CommandResult ok(core::Value value) {
        CommandResult result;
        result.value = std::move(value);
        return result;
    }

    static CommandResult done() {
        return CommandResult{};
    }

    static CommandResult failure(
        std::string message,
        std::optional<core::Span> span = std::nullopt,
        core::SourceId source = core::kNoSource) {
        CommandResult result;
        result.error = HostError{std::move(message), span, source};
        return result;
    }
};

// void 命令可以直接写 Output；值函数默认不接收 Output，但宿主函数仍可
// 通过捕获状态做显式 I/O（例如 ReferenceHost::read）。
using CommandHandler = std::function<CommandResult(
    const std::vector<core::Value>&, const core::CommandContext&,
    core::Output&)>;

using FunctionHandler = std::function<CommandResult(
    const std::vector<core::Value>&, const core::CommandContext&)>;

} // namespace lush::command

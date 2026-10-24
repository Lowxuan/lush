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

struct HostError {
    std::string message;
    std::optional<core::Span> span;
};

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

    static CommandResult failure(std::string message,
                                 std::optional<core::Span> span = std::nullopt) {
        CommandResult result;
        result.error = HostError{std::move(message), span};
        return result;
    }
};

// Void commands may write to the output stream.  Value functions are pure
// callables; they return a value and do not receive an Output.
using CommandHandler = std::function<CommandResult(
    const std::vector<core::Value>&, const core::CommandContext&,
    core::Output&)>;

using FunctionHandler = std::function<CommandResult(
    const std::vector<core::Value>&, const core::CommandContext&)>;

} // namespace lush::command

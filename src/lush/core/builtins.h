#pragma once

#include "lush/core/output.h"
#include "lush/core/value.h"

#include <optional>
#include <string_view>
#include <vector>

namespace lush::core {

struct CommandContext {
    Span nameSpan{};
    std::vector<Span> argSpans;
};

class NameCatalog {
public:
    virtual ~NameCatalog() = default;

    virtual CallablePtr findCallable(std::string_view name) const = 0;
    virtual bool isCommand(std::string_view name) const = 0;
    virtual bool isReserved(std::string_view name) const = 0;
    virtual std::optional<Diagnostic> invokeCommand(
        std::string_view name, const std::vector<Value>& args, Output& output,
        const CommandContext& context) const = 0;
};

class BuiltinCatalog final : public NameCatalog {
public:
    CallablePtr findCallable(std::string_view name) const override;
    bool isCommand(std::string_view name) const override;
    bool isReserved(std::string_view name) const override;
    std::optional<Diagnostic> invokeCommand(
        std::string_view name, const std::vector<Value>& args, Output& output,
        const CommandContext& context) const override;
};

} // namespace lush::core

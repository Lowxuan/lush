#pragma once

#include "lush/core/output.h"
#include "lush/core/value.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace lush::core {

struct CommandContext {
    Span nameSpan{};
    SourceId nameSource = kNoSource;
    std::vector<Span> argSpans;
    std::vector<SourceId> argSources;
};

// The language-level shape of a registered callable.  This deliberately does
// not describe parameter types: validation only needs arity and interaction
// metadata, while type checking happens in the callable or command binder.
struct CallableInfo {
    std::size_t required = 0;
    std::size_t maxFixed = 0;
    bool variadic = false;
    bool interactive = false;
    bool returnsValue = false;
};

class NameCatalog {
public:
    virtual ~NameCatalog() = default;

    virtual std::optional<CallableInfo> callableInfo(
        std::string_view name) const = 0;
    virtual CallablePtr findCallable(std::string_view name) const = 0;
    virtual std::optional<Diagnostic> invokeCommand(
        std::string_view name, const std::vector<Value>& args, Output& output,
        const CommandContext& context) const = 0;

    bool isCommand(std::string_view name) const;
    bool isReserved(std::string_view name) const;
};

class BuiltinCatalog final : public NameCatalog {
public:
    std::optional<CallableInfo> callableInfo(
        std::string_view name) const override;
    CallablePtr findCallable(std::string_view name) const override;
    std::optional<Diagnostic> invokeCommand(
        std::string_view name, const std::vector<Value>& args, Output& output,
        const CommandContext& context) const override;
};

} // namespace lush::core

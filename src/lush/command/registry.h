#pragma once

#include "lush/command/host.h"
#include "lush/command/spec.h"
#include "lush/core/builtins.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lush::command {

enum class RegistrationError {
    EmptyName,
    InvalidName,
    Keyword,
    DuplicateName,
    ReservedBuiltin,
    RequiredAfterOptional,
    MultipleVariadic,
    VariadicNotLast,
    VariadicOptional,
    VariadicDefault,
    OptionalWithoutDefault,
    RequiredWithDefault,
    DuplicateParam,
    InvalidDim,
    DefaultTypeMismatch,
    InteractiveValue,
    ReturnKindMismatch,
    ReturnTypeMismatch,
    InvalidReturnDim,
    EmptyHandler,
};

struct RegistrationResult {
    std::optional<RegistrationError> error;
    std::string message;

    bool ok() const noexcept {
        return !error.has_value();
    }
};

// Shared parameter conversion used by full command binding and by interactive
// sessions.  The caller supplies the span/source for a diagnostic.
struct ParamBindResult {
    std::optional<core::Value> value;
    std::optional<core::ErrorCode> error;
    std::string message;
};

ParamBindResult bindParam(const ParamSpec& param, const core::Value& value);

struct RegisteredCallable {
    Signature signature;
    CommandHandler commandHandler;
    FunctionHandler functionHandler;
};

class Registry final : public core::NameCatalog {
public:
    Registry() = default;

    RegistrationResult registerCommand(CommandSpec spec,
                                      CommandHandler handler);
    RegistrationResult registerFunction(CommandSpec spec,
                                       FunctionHandler handler);

    const Signature* findSignature(std::string_view name) const;

    // Canonical signatures for completion; aliases remain inside each
    // Signature and are not duplicated in the returned vector.
    std::vector<Signature> signatures() const;

    std::optional<core::CallableInfo> callableInfo(
        std::string_view name) const override;
    core::CallablePtr findCallable(std::string_view name) const override;
    std::optional<core::Diagnostic> invokeCommand(
        std::string_view name, const std::vector<core::Value>& args,
        core::Output& output,
        const core::CommandContext& context) const override;

private:
    RegistrationResult registerImpl(CommandSpec spec,
                                   CommandHandler commandHandler,
                                   FunctionHandler functionHandler);

    std::shared_ptr<const RegisteredCallable> findRegistered(
        std::string_view name) const;

    std::unordered_map<std::string, std::shared_ptr<const RegisteredCallable>>
        names_;
    std::unordered_map<std::string, core::CallablePtr> callables_;
    core::BuiltinCatalog builtins_;
};

} // namespace lush::command

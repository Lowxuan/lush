#include "lush/command/registry.h"

#include "lush/core/text.h"

// 命令注册、参数绑定和宿主 callable 适配。
//
// 注册阶段把 CommandSpec 转成 Signature；调用阶段先检查 arity，再按
// 参数从左到右转换，最后才调用 handler。注册失败是宿主编程错误，
// 返回 RegistrationResult，不产生 L/P/S/R 诊断。

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lush::command {
namespace {

using core::asciiLower;

bool isAsciiAlpha(char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
}

bool isAsciiDigit(char character) {
    return character >= '0' && character <= '9';
}

bool isIdentifier(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    if (!isAsciiAlpha(text.front()) && text.front() != '_') {
        return false;
    }
    for (char character : text.substr(1)) {
        if (!isAsciiAlpha(character) && !isAsciiDigit(character) &&
            character != '_') {
            return false;
        }
    }
    return true;
}

bool isKeyword(std::string_view text) {
    const std::string lower = asciiLower(text);
    return lower == "if" || lower == "else" || lower == "while" ||
           lower == "for" || lower == "in" || lower == "true" ||
           lower == "false" || lower == "ref" || lower == "break" ||
           lower == "continue" || lower == "def" || lower == "return";
}

std::string_view paramTypeName(ParamType type) {
    switch (type) {
    case ParamType::Int: return "int";
    case ParamType::Number: return "number";
    case ParamType::Point: return "point";
    case ParamType::String: return "string";
    case ParamType::Bool: return "bool";
    case ParamType::List: return "list";
    case ParamType::Any: return "any";
    }
    return "any";
}

bool numericValue(const core::Value& value, double& out) {
    if (value.isInt()) {
        out = static_cast<double>(value.asInt());
        return true;
    }
    if (value.isDouble()) {
        out = value.asDouble();
        return true;
    }
    return false;
}

bool pointFromList(const core::ListValue& list, core::PointValue& out) {
    const std::size_t size = list.size();
    if (size != 2 && size != 3) {
        return false;
    }

    core::PointValue point;
    point.dim = static_cast<int>(size);
    for (std::size_t index = 0; index < size; ++index) {
        double coordinate = 0.0;
        if (!numericValue(list.items()[index], coordinate)) {
            return false;
        }
        point.coordinates[index] = coordinate;
    }
    out = point;
    return true;
}

// 参数转换结果：成功值、失败码和错误文案三选一组合。
struct ConvertResult {
    std::optional<core::Value> value;
    std::optional<core::ErrorCode> error;
    std::string message;
};

ConvertResult converted(core::Value value) {
    return ConvertResult{std::move(value), std::nullopt, {}};
}

ConvertResult conversionError(core::ErrorCode code, std::string message) {
    return ConvertResult{std::nullopt, code, std::move(message)};
}

ConvertResult typeError(const ParamSpec& param, const core::Value& value) {
    return conversionError(
        core::ErrorCode::R002,
        "期望 " + std::string(paramTypeName(param.type)) + "，实际为 " +
            core::runtimeTypeName(value));
}

ConvertResult convertParam(const ParamSpec& param, const core::Value& value) {
    switch (param.type) {
    case ParamType::Int:
        if (value.isInt()) {
            return converted(value);
        }
        return typeError(param, value);
    case ParamType::Number:
        if (value.isInt()) {
            return converted(
                core::Value::makeDouble(static_cast<double>(value.asInt())));
        }
        if (value.isDouble()) {
            return converted(value);
        }
        return typeError(param, value);
    case ParamType::Point:
        if (value.isPoint()) {
            const core::PointValue& point = value.asPoint();
            if (param.dim != 0 && point.dim != param.dim) {
                return conversionError(core::ErrorCode::R002,
                                       "point 维度不符");
            }
            return converted(value);
        }
        if (value.isList()) {
            core::PointValue point;
            if (!pointFromList(value.asList(), point)) {
                return conversionError(
                    core::ErrorCode::R006,
                    "无法构造 point：list 长度必须是 2 或 3，"
                    "且元素必须是数值");
            }
            if (param.dim != 0 && point.dim != param.dim) {
                return conversionError(core::ErrorCode::R002,
                                       "point 维度不符");
            }
            return converted(core::Value::makePoint(point));
        }
        return typeError(param, value);
    case ParamType::String:
        if (value.isString()) {
            return converted(value);
        }
        return typeError(param, value);
    case ParamType::Bool:
        if (value.isBool()) {
            return converted(value);
        }
        return typeError(param, value);
    case ParamType::List:
        if (value.isList()) {
            return converted(value);
        }
        return typeError(param, value);
    case ParamType::Any:
        return converted(value);
    }
    return typeError(param, value);
}

// 完整绑定结果：绑定后的参数值，或首个参数/arity 诊断。
struct BindResult {
    std::vector<core::Value> values;
    std::optional<core::Diagnostic> diagnostic;
};

core::Diagnostic makeDiagnostic(core::ErrorCode code, core::Span span,
                                std::string message,
                                core::SourceId source = core::kNoSource) {
    return core::Diagnostic{code, span, std::move(message), {}, source};
}

core::Diagnostic makeHostDiagnostic(const HostError& error,
                                    core::Span defaultSpan,
                                    core::SourceId defaultSource) {
    std::string message = error.message;
    if (message.empty()) {
        message = "命令执行失败";
    }
    const core::SourceId source =
        error.source != core::kNoSource ? error.source : defaultSource;
    return makeDiagnostic(core::ErrorCode::R010,
                          error.span.value_or(defaultSpan),
                          std::move(message), source);
}

// 绑定命令实参：检查 arity、补默认值、转换类型并处理变参。
// callSite=true 时 arity 错误为 R002；命令语句为 S002/S006。
BindResult bindArguments(const Signature& signature,
                         const std::vector<core::Value>& args,
                         const std::vector<core::Span>& argSpans,
                         const std::vector<core::SourceId>& argSources,
                         core::Span aritySpan, core::SourceId aritySource,
                         bool callSite) {
    BindResult result;
    const std::size_t argc = args.size();

    const auto arityCode = [&]() {
        if (callSite) {
            return core::ErrorCode::R002;
        }
        return signature.interactive ? core::ErrorCode::S006
                                     : core::ErrorCode::S002;
    };

    if (argc < signature.required) {
        result.diagnostic = makeDiagnostic(arityCode(), aritySpan,
                                           "参数个数不符", aritySource);
        return result;
    }
    if (!signature.variadic && argc > signature.maxFixed) {
        result.diagnostic = makeDiagnostic(arityCode(), aritySpan,
                                           "参数个数不符", aritySource);
        return result;
    }

    result.values.reserve(argc);

    for (std::size_t index = 0; index < signature.maxFixed; ++index) {
        const ParamSpec& param = signature.params[index];
        if (index < argc) {
            const core::Value& value = args[index];
            ConvertResult convertedValue = convertParam(param, value);
            if (!convertedValue.value) {
                const core::Span span =
                    index < argSpans.size() ? argSpans[index] : aritySpan;
                const core::SourceId source =
                    index < argSources.size() ? argSources[index]
                                              : aritySource;
                result.diagnostic = makeDiagnostic(
                    convertedValue.error.value_or(core::ErrorCode::R002),
                    span, std::move(convertedValue.message), source);
                return result;
            }
            result.values.push_back(std::move(*convertedValue.value));
            continue;
        }

        if (!param.optional || !param.defaultValue) {
            result.diagnostic = makeDiagnostic(arityCode(), aritySpan,
                                               "参数个数不符", aritySource);
            return result;
        }
        result.values.push_back(*param.defaultValue);
    }

    if (signature.variadic) {
        const ParamSpec& param = signature.params.back();
        for (std::size_t index = signature.maxFixed; index < argc; ++index) {
            ConvertResult convertedValue = convertParam(param, args[index]);
            if (!convertedValue.value) {
                const core::Span span =
                    index < argSpans.size() ? argSpans[index] : aritySpan;
                const core::SourceId source =
                    index < argSources.size() ? argSources[index]
                                              : aritySource;
                result.diagnostic = makeDiagnostic(
                    convertedValue.error.value_or(core::ErrorCode::R002),
                    span, std::move(convertedValue.message), source);
                return result;
            }
            result.values.push_back(std::move(*convertedValue.value));
        }
    }

    return result;
}

BindResult bindStatement(const Signature& signature,
                         const std::vector<core::Value>& args,
                         const std::vector<core::Span>& argSpans,
                         const std::vector<core::SourceId>& argSources,
                         core::Span nameSpan, core::SourceId nameSource) {
    return bindArguments(signature, args, argSpans, argSources, nameSpan,
                         nameSource, false);
}

BindResult bindCall(const Signature& signature,
                    const std::vector<core::Value>& args,
                    const std::vector<core::Span>& argSpans,
                    core::Span lparenSpan, core::SourceId source) {
    const std::vector<core::SourceId> argSources(argSpans.size(), source);
    return bindArguments(signature, args, argSpans, argSources, lparenSpan,
                         source, true);
}

// 把注册的值函数包装成 core::Callable；共享 RegisteredCallable 所有权。
class HostCallable final : public core::Callable {
public:
    explicit HostCallable(
        std::shared_ptr<const RegisteredCallable> registered)
        : registered_(std::move(registered)) {}

    std::string_view name() const noexcept override {
        return registered_->signature.name;
    }

    core::CallResult call(const std::vector<core::Value>& args,
                          const core::CallContext& context) const override {
        const BindResult bound =
            bindCall(registered_->signature, args, context.argSpans,
                     context.lparenSpan, context.source);
        if (bound.diagnostic) {
            return core::CallResult::error(*bound.diagnostic);
        }
        if (!registered_->functionHandler) {
            return core::CallResult::error(
                makeDiagnostic(core::ErrorCode::R010, context.calleeSpan,
                               "宿主函数没有处理器", context.source));
        }

        core::CommandContext commandContext;
        commandContext.nameSpan = context.calleeSpan;
        commandContext.nameSource = context.source;
        commandContext.argSpans = context.argSpans;
        commandContext.argSources.assign(context.argSpans.size(),
                                         context.source);

        CommandResult result =
            registered_->functionHandler(bound.values, commandContext);
        if (result.error) {
            return core::CallResult::error(makeHostDiagnostic(
                *result.error, context.calleeSpan, context.source));
        }
        if (!result.value) {
            return core::CallResult::error(
                makeDiagnostic(core::ErrorCode::R010, context.calleeSpan,
                               "宿主函数没有返回值", context.source));
        }
        return core::CallResult::ok(std::move(*result.value));
    }

private:
    std::shared_ptr<const RegisteredCallable> registered_;
};

RegistrationResult registrationError(RegistrationError code,
                                     std::string message) {
    RegistrationResult result;
    result.error = code;
    result.message = std::move(message);
    return result;
}

} // namespace

ParamBindResult bindParam(const ParamSpec& param, const core::Value& value) {
    ConvertResult converted = convertParam(param, value);
    return ParamBindResult{std::move(converted.value), converted.error,
                           std::move(converted.message)};
}

RegistrationResult Registry::registerCommand(
    CommandSpec spec, CommandHandler handler) {
    if (!handler) {
        return registrationError(RegistrationError::EmptyHandler,
                                 "命令没有处理器");
    }
    if (spec.returns != ReturnKind::Void) {
        return registrationError(RegistrationError::ReturnKindMismatch,
                                 "registerCommand 只接受 void 命令");
    }
    return registerImpl(std::move(spec), std::move(handler), {});
}

RegistrationResult Registry::registerFunction(
    CommandSpec spec, FunctionHandler handler) {
    if (!handler) {
        return registrationError(RegistrationError::EmptyHandler,
                                 "函数没有处理器");
    }
    if (spec.returns != ReturnKind::Value) {
        return registrationError(RegistrationError::ReturnKindMismatch,
                                 "registerFunction 只接受 value 函数");
    }
    if (spec.interactive) {
        return registrationError(RegistrationError::InteractiveValue,
                                 "value 函数不能是 interactive");
    }
    return registerImpl(std::move(spec), {}, std::move(handler));
}

RegistrationResult Registry::registerImpl(
    CommandSpec spec, CommandHandler commandHandler,
    FunctionHandler functionHandler) {
    if (spec.name.empty()) {
        return registrationError(RegistrationError::EmptyName,
                                 "命令名不能为空");
    }
    if (!isIdentifier(spec.name)) {
        return registrationError(RegistrationError::InvalidName,
                                 "命令名必须是合法标识符");
    }
    if (isKeyword(spec.name)) {
        return registrationError(RegistrationError::Keyword,
                                 "命令名不能是关键字");
    }
    if (spec.interactive && spec.returns == ReturnKind::Value) {
        return registrationError(RegistrationError::InteractiveValue,
                                 "value 函数不能是 interactive");
    }
    if (spec.returnType) {
        if (spec.returns == ReturnKind::Void) {
            return registrationError(RegistrationError::ReturnTypeMismatch,
                                     "void 命令不能声明 returnType");
        }
        if (spec.returnType->type == core::ValueType::Point) {
            const int dim = spec.returnType->dim;
            if (dim != 0 && dim != 2 && dim != 3) {
                return registrationError(RegistrationError::InvalidReturnDim,
                                         "point 返回维度必须是 0、2 或 3");
            }
        } else if (spec.returnType->dim != 0) {
            return registrationError(RegistrationError::InvalidReturnDim,
                                     "只有 point 返回类型可以设置 dim");
        }
    }

    std::vector<std::string> keys;
    keys.reserve(1 + spec.aliases.size());
    keys.push_back(spec.name);

    std::unordered_set<std::string> localKeys;
    localKeys.insert(asciiLower(spec.name));

    for (const std::string& alias : spec.aliases) {
        if (alias.empty()) {
            return registrationError(RegistrationError::EmptyName,
                                     "别名不能为空");
        }
        if (!isIdentifier(alias)) {
            return registrationError(RegistrationError::InvalidName,
                                     "别名必须是合法标识符");
        }
        if (isKeyword(alias)) {
            return registrationError(RegistrationError::Keyword,
                                     "别名不能是关键字");
        }
        const std::string key = asciiLower(alias);
        if (!localKeys.insert(key).second) {
            return registrationError(RegistrationError::DuplicateName,
                                     "名称或别名重复");
        }
        keys.push_back(alias);
    }

    for (const std::string& key : keys) {
        const std::string lower = asciiLower(key);
        if (builtins_.isReserved(key)) {
            return registrationError(RegistrationError::ReservedBuiltin,
                                     "名称与内建冲突：" + key);
        }
        if (names_.find(lower) != names_.end()) {
            return registrationError(RegistrationError::DuplicateName,
                                     "名称已注册：" + key);
        }
    }

    std::unordered_set<std::string> paramNames;
    bool sawOptional = false;
    bool sawVariadic = false;

    for (std::size_t index = 0; index < spec.params.size(); ++index) {
        ParamSpec& param = spec.params[index];
        if (param.name.empty()) {
            return registrationError(RegistrationError::InvalidName,
                                     "参数名不能为空");
        }
        if (!paramNames.insert(param.name).second) {
            return registrationError(RegistrationError::DuplicateParam,
                                     "参数名重复：" + param.name);
        }

        if (param.type == ParamType::Point) {
            if (param.dim != 0 && param.dim != 2 && param.dim != 3) {
                return registrationError(RegistrationError::InvalidDim,
                                         "point 维度必须是 0、2 或 3");
            }
        } else if (param.dim != 0) {
            return registrationError(RegistrationError::InvalidDim,
                                     "只有 point 参数可以设置 dim");
        }

        if (param.variadic) {
            if (sawVariadic) {
                return registrationError(RegistrationError::MultipleVariadic,
                                         "只能有一个变参");
            }
            if (index + 1 != spec.params.size()) {
                return registrationError(RegistrationError::VariadicNotLast,
                                         "变参必须是最后一个参数");
            }
            if (param.optional) {
                return registrationError(RegistrationError::VariadicOptional,
                                         "变参不能是 optional");
            }
            if (param.defaultValue) {
                return registrationError(RegistrationError::VariadicDefault,
                                         "变参不能有默认值");
            }
            if (sawOptional) {
                return registrationError(RegistrationError::VariadicOptional,
                                         "变参命令不能有可选参数");
            }
            sawVariadic = true;
            continue;
        }

        if (param.optional) {
            sawOptional = true;
            if (!param.defaultValue) {
                return registrationError(
                    RegistrationError::OptionalWithoutDefault,
                    "可选参数必须有默认值");
            }
            if (param.defaultValue->isCallable()) {
                return registrationError(
                    RegistrationError::DefaultTypeMismatch,
                    "默认值不能是 callable");
            }
            ConvertResult convertedDefault =
                convertParam(param, *param.defaultValue);
            if (!convertedDefault.value) {
                return registrationError(
                    RegistrationError::DefaultTypeMismatch,
                    "默认值无法绑定到参数 " + param.name);
            }
            param.defaultValue = std::move(*convertedDefault.value);
        } else {
            if (sawOptional) {
                return registrationError(
                    RegistrationError::RequiredAfterOptional,
                    "必选参数不能排在可选参数之后");
            }
            if (param.defaultValue) {
                return registrationError(
                    RegistrationError::RequiredWithDefault,
                    "必选参数不能有默认值");
            }
        }
    }

    Signature signature;
    signature.name = spec.name;
    signature.aliases = std::move(spec.aliases);
    signature.params = std::move(spec.params);
    signature.help = std::move(spec.help);
    signature.variadic = sawVariadic;
    signature.interactive = spec.interactive;
    signature.returnsValue = spec.returns == ReturnKind::Value;
    signature.returnType = spec.returnType;
    for (const ParamSpec& param : signature.params) {
        if (param.variadic) {
            continue;
        }
        ++signature.maxFixed;
        if (!param.optional) {
            ++signature.required;
        }
    }

    auto registered = std::make_shared<RegisteredCallable>();
    registered->signature = std::move(signature);
    registered->commandHandler = std::move(commandHandler);
    registered->functionHandler = std::move(functionHandler);

    core::CallablePtr callable;
    if (registered->signature.returnsValue) {
        callable = std::make_shared<HostCallable>(registered);
    }

    for (const std::string& key : keys) {
        const std::string lower = asciiLower(key);
        names_.emplace(lower, registered);
        if (callable) {
            callables_.emplace(lower, callable);
        }
    }

    return RegistrationResult{};
}

const Signature* Registry::findSignature(std::string_view name) const {
    const std::shared_ptr<const RegisteredCallable> registered =
        findRegistered(name);
    return registered ? &registered->signature : nullptr;
}

std::vector<Signature> Registry::signatures() const {
    std::vector<Signature> result;
    result.reserve(names_.size());

    std::unordered_set<const RegisteredCallable*> seen;
    for (const auto& entry : names_) {
        const RegisteredCallable* registered = entry.second.get();
        if (seen.insert(registered).second) {
            result.push_back(entry.second->signature);
        }
    }
    return result;
}

std::optional<core::CallableInfo> Registry::callableInfo(
    std::string_view name) const {
    if (const std::shared_ptr<const RegisteredCallable> registered =
            findRegistered(name)) {
        const Signature& signature = registered->signature;
        return core::CallableInfo{signature.required, signature.maxFixed,
                                  signature.variadic, signature.interactive,
                                  signature.returnsValue};
    }
    return builtins_.callableInfo(name);
}

core::CallablePtr Registry::findCallable(std::string_view name) const {
    const std::string lower = asciiLower(name);
    if (const auto callable = callables_.find(lower);
        callable != callables_.end()) {
        return callable->second;
    }
    if (names_.find(lower) != names_.end()) {
        return nullptr;
    }
    return builtins_.findCallable(name);
}

std::optional<core::Diagnostic> Registry::invokeCommand(
    std::string_view name, const std::vector<core::Value>& args,
    core::Output& output, const core::CommandContext& context) const {
    const std::shared_ptr<const RegisteredCallable> registered =
        findRegistered(name);
    if (!registered) {
        return builtins_.invokeCommand(name, args, output, context);
    }

    const BindResult bound =
        bindStatement(registered->signature, args, context.argSpans,
                      context.argSources, context.nameSpan,
                      context.nameSource);
    if (bound.diagnostic) {
        return bound.diagnostic;
    }

    if (registered->signature.returnsValue) {
        if (!registered->functionHandler) {
            return makeDiagnostic(core::ErrorCode::R010, context.nameSpan,
                                  "宿主函数没有处理器", context.nameSource);
        }
        CommandResult result =
            registered->functionHandler(bound.values, context);
        if (result.error) {
            return makeHostDiagnostic(*result.error, context.nameSpan,
                                      context.nameSource);
        }
        if (!result.value) {
            return makeDiagnostic(core::ErrorCode::R010, context.nameSpan,
                                  "宿主函数没有返回值",
                                  context.nameSource);
        }
        return std::nullopt;
    }

    if (!registered->commandHandler) {
        return makeDiagnostic(core::ErrorCode::R010, context.nameSpan,
                              "宿主命令没有处理器", context.nameSource);
    }
    CommandResult result =
        registered->commandHandler(bound.values, context, output);
    if (result.error) {
        return makeHostDiagnostic(*result.error, context.nameSpan,
                                  context.nameSource);
    }
    return std::nullopt;
}

std::shared_ptr<const RegisteredCallable> Registry::findRegistered(
    std::string_view name) const {
    const auto found = names_.find(asciiLower(name));
    if (found == names_.end()) {
        return nullptr;
    }
    return found->second;
}

} // namespace lush::command

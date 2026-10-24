#include "lush/core/builtins.h"

#include "lush/core/format.h"
#include "lush/core/utf8.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace lush::core {
namespace {

enum class BuiltinId {
    Int,
    Double,
    Bool,
    String,
    List,
    Point,
    Type,
    Len,
};

struct BuiltinArity {
    std::size_t required = 1;
    std::size_t maxFixed = 1;
    bool variadic = false;
};

BuiltinArity builtinArity(BuiltinId id) {
    if (id == BuiltinId::Point) {
        return BuiltinArity{1, 3, false};
    }
    return BuiltinArity{1, 1, false};
}

bool arityOk(const BuiltinArity& arity, std::size_t argc) {
    return argc >= arity.required &&
           (arity.variadic || argc <= arity.maxFixed);
}

CallablePtr callableForType(ValueType type);

class BuiltinCallable final : public Callable {
public:
    explicit BuiltinCallable(BuiltinId id) : id_(id) {}

    std::string_view name() const noexcept override {
        switch (id_) {
        case BuiltinId::Int: return "int";
        case BuiltinId::Double: return "double";
        case BuiltinId::Bool: return "bool";
        case BuiltinId::String: return "string";
        case BuiltinId::List: return "list";
        case BuiltinId::Point: return "point";
        case BuiltinId::Type: return "type";
        case BuiltinId::Len: return "len";
        }
        return "callable";
    }

    CallResult call(const std::vector<Value>& args,
                    const CallContext& context) const override;

private:
    BuiltinId id_;
};

std::string asciiLower(std::string_view text) {
    std::string lower;
    lower.reserve(text.size());
    for (char character : text) {
        if (character >= 'A' && character <= 'Z') {
            lower.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            lower.push_back(character);
        }
    }
    return lower;
}

bool asciiEqualsIgnoreCase(std::string_view left, std::string_view right) {
    return asciiLower(left) == asciiLower(right);
}

std::optional<BuiltinId> findBuiltinId(std::string_view name) {
    const std::string lower = asciiLower(name);
    if (lower == "int") {
        return BuiltinId::Int;
    }
    if (lower == "double") {
        return BuiltinId::Double;
    }
    if (lower == "bool") {
        return BuiltinId::Bool;
    }
    if (lower == "string" || lower == "str") {
        return BuiltinId::String;
    }
    if (lower == "list") {
        return BuiltinId::List;
    }
    if (lower == "point") {
        return BuiltinId::Point;
    }
    if (lower == "type") {
        return BuiltinId::Type;
    }
    if (lower == "len") {
        return BuiltinId::Len;
    }
    return std::nullopt;
}

std::string actualTypeName(const Value& value) {
    if (value.isCallable()) {
        return "callable";
    }
    return std::string(valueTypeName(value.type()));
}

Span argumentSpan(const CallContext& context, std::size_t index) {
    if (index < context.argSpans.size()) {
        return context.argSpans[index];
    }
    return context.calleeSpan;
}

CallResult arityError(const CallContext& context) {
    return CallResult::error(
        Diagnostic{ErrorCode::R002, context.lparenSpan, "参数个数不符", {}});
}

CallResult typeError(const CallContext& context, std::size_t index,
                     const std::string& expected, const Value& actual) {
    return CallResult::error(Diagnostic{
        ErrorCode::R002, argumentSpan(context, index),
        "期望 " + expected + "，实际为 " + actualTypeName(actual), {}});
}

bool numericValue(const Value& value, double& out) {
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

bool pointFromList(const ListValue& list, PointValue& out) {
    const std::size_t size = list.size();
    if (size != 2 && size != 3) {
        return false;
    }

    PointValue point;
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

CallablePtr intCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::Int);
    return callable;
}

CallablePtr doubleCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::Double);
    return callable;
}

CallablePtr boolCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::Bool);
    return callable;
}

CallablePtr stringCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::String);
    return callable;
}

CallablePtr listCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::List);
    return callable;
}

CallablePtr pointCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::Point);
    return callable;
}

CallablePtr typeCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::Type);
    return callable;
}

CallablePtr lenCallable() {
    static const CallablePtr callable =
        std::make_shared<BuiltinCallable>(BuiltinId::Len);
    return callable;
}

CallablePtr callableForBuiltin(BuiltinId id) {
    switch (id) {
    case BuiltinId::Int: return intCallable();
    case BuiltinId::Double: return doubleCallable();
    case BuiltinId::Bool: return boolCallable();
    case BuiltinId::String: return stringCallable();
    case BuiltinId::List: return listCallable();
    case BuiltinId::Point: return pointCallable();
    case BuiltinId::Type: return typeCallable();
    case BuiltinId::Len: return lenCallable();
    }
    return intCallable();
}

CallResult callInt(const std::vector<Value>& args, const CallContext& context) {
    const Value& value = args[0];
    if (value.isInt()) {
        return CallResult::ok(value);
    }
    if (!value.isDouble()) {
        return typeError(context, 0, "int 或 double", value);
    }

    const double number = value.asDouble();
    constexpr double limit = 9223372036854775808.0;
    if (!std::isfinite(number) || number >= limit || number < -limit) {
        return CallResult::error(Diagnostic{
            ErrorCode::R005, argumentSpan(context, 0), "整数越界", {}});
    }

    return CallResult::ok(Value::makeInt(
        static_cast<std::int64_t>(std::trunc(number))));
}

CallResult callDouble(const std::vector<Value>& args,
                      const CallContext& context) {
    const Value& value = args[0];
    if (value.isDouble()) {
        return CallResult::ok(value);
    }
    if (value.isInt()) {
        return CallResult::ok(
            Value::makeDouble(static_cast<double>(value.asInt())));
    }
    return typeError(context, 0, "int 或 double", value);
}

CallResult callBool(const std::vector<Value>& args,
                    const CallContext& context) {
    const Value& value = args[0];
    switch (value.type()) {
    case ValueType::Bool:
        return CallResult::ok(value);
    case ValueType::Int:
        return CallResult::ok(Value::makeBool(value.asInt() != 0));
    case ValueType::Double:
        return CallResult::ok(Value::makeBool(value.asDouble() != 0.0));
    case ValueType::String:
        return CallResult::ok(Value::makeBool(!value.asString().empty()));
    case ValueType::List:
        return CallResult::ok(Value::makeBool(!value.asList().empty()));
    case ValueType::Point:
    case ValueType::Callable:
        return typeError(context, 0, "bool/int/double/string/list", value);
    }
    return typeError(context, 0, "bool/int/double/string/list", value);
}

CallResult callString(const std::vector<Value>& args,
                      const CallContext& context) {
    (void)context;
    return CallResult::ok(Value::makeString(formatValue(args[0])));
}

CallResult callList(const std::vector<Value>& args,
                    const CallContext& context) {
    const Value& value = args[0];
    if (value.isList()) {
        return CallResult::ok(Value::makeList(value.asList()));
    }

    if (value.isString()) {
        const std::string& text = value.asString();
        std::vector<Value> items;
        const std::size_t length = utf8Length(text);
        items.reserve(length);
        for (std::size_t index = 0; index < length; ++index) {
            const std::optional<std::string> codePoint = utf8At(text, index);
            if (!codePoint) {
                break;
            }
            items.push_back(Value::makeString(*codePoint));
        }
        return CallResult::ok(Value::makeList(ListValue(std::move(items))));
    }

    if (value.isPoint()) {
        const PointValue& point = value.asPoint();
        std::vector<Value> items;
        items.reserve(static_cast<std::size_t>(point.dim));
        for (int index = 0; index < point.dim; ++index) {
            items.push_back(Value::makeDouble(point[index]));
        }
        return CallResult::ok(Value::makeList(ListValue(std::move(items))));
    }

    return typeError(context, 0, "list/string/point", value);
}

CallResult callPoint(const std::vector<Value>& args,
                     const CallContext& context) {
    if (args.size() == 1) {
        const Value& value = args[0];
        if (value.isPoint()) {
            return CallResult::ok(value);
        }
        if (value.isList()) {
            PointValue point;
            if (!pointFromList(value.asList(), point)) {
                return CallResult::error(Diagnostic{
                    ErrorCode::R006, argumentSpan(context, 0),
                    "无法构造 point：list 长度必须是 2 或 3，且元素必须是数值",
                    {}});
            }
            return CallResult::ok(Value::makePoint(point));
        }
        return typeError(context, 0, "point 或 list", value);
    }

    PointValue point;
    point.dim = static_cast<int>(args.size());
    for (std::size_t index = 0; index < args.size(); ++index) {
        double coordinate = 0.0;
        if (!numericValue(args[index], coordinate)) {
            return typeError(context, index, "int 或 double", args[index]);
        }
        point.coordinates[index] = coordinate;
    }
    return CallResult::ok(Value::makePoint(point));
}

CallResult callType(const std::vector<Value>& args,
                    const CallContext& context) {
    (void)context;
    return CallResult::ok(Value::makeCallable(callableForType(args[0].type())));
}

CallResult callLen(const std::vector<Value>& args, const CallContext& context) {
    const Value& value = args[0];
    switch (value.type()) {
    case ValueType::List:
        return CallResult::ok(
            Value::makeInt(static_cast<std::int64_t>(value.asList().size())));
    case ValueType::String:
        return CallResult::ok(Value::makeInt(
            static_cast<std::int64_t>(utf8Length(value.asString()))));
    case ValueType::Point:
        return CallResult::ok(Value::makeInt(value.asPoint().dim));
    case ValueType::Int:
    case ValueType::Double:
    case ValueType::Bool:
    case ValueType::Callable:
        return typeError(context, 0, "list/string/point", value);
    }
    return typeError(context, 0, "list/string/point", value);
}

CallResult BuiltinCallable::call(const std::vector<Value>& args,
                                 const CallContext& context) const {
    if (!arityOk(builtinArity(id_), args.size())) {
        return arityError(context);
    }

    switch (id_) {
    case BuiltinId::Int: return callInt(args, context);
    case BuiltinId::Double: return callDouble(args, context);
    case BuiltinId::Bool: return callBool(args, context);
    case BuiltinId::String: return callString(args, context);
    case BuiltinId::List: return callList(args, context);
    case BuiltinId::Point: return callPoint(args, context);
    case BuiltinId::Type: return callType(args, context);
    case BuiltinId::Len: return callLen(args, context);
    }
    return CallResult::error(Diagnostic{
        ErrorCode::R002, context.calleeSpan, "未知 callable", {}});
}

CallablePtr callableForType(ValueType type) {
    switch (type) {
    case ValueType::Int: return intCallable();
    case ValueType::Double: return doubleCallable();
    case ValueType::Bool: return boolCallable();
    case ValueType::String: return stringCallable();
    case ValueType::List: return listCallable();
    case ValueType::Point: return pointCallable();
    case ValueType::Callable: return typeCallable();
    }
    return typeCallable();
}

} // namespace

bool NameCatalog::isCommand(std::string_view name) const {
    const std::optional<CallableInfo> info = callableInfo(name);
    return info.has_value() && !info->returnsValue;
}

bool NameCatalog::isReserved(std::string_view name) const {
    return callableInfo(name).has_value();
}

std::optional<CallableInfo> BuiltinCatalog::callableInfo(
    std::string_view name) const {
    if (asciiEqualsIgnoreCase(name, "echo")) {
        return CallableInfo{0, 0, true, false, false};
    }

    if (const std::optional<BuiltinId> id = findBuiltinId(name)) {
        const BuiltinArity arity = builtinArity(*id);
        return CallableInfo{arity.required, arity.maxFixed, arity.variadic,
                            false, true};
    }
    return std::nullopt;
}

CallablePtr BuiltinCatalog::findCallable(std::string_view name) const {
    const std::optional<BuiltinId> id = findBuiltinId(name);
    if (!id) {
        return nullptr;
    }
    return callableForBuiltin(*id);
}

std::optional<Diagnostic> BuiltinCatalog::invokeCommand(
    std::string_view name, const std::vector<Value>& args, Output& output,
    const CommandContext& context) const {
    const std::optional<CallableInfo> info = callableInfo(name);
    if (!info) {
        return Diagnostic{ErrorCode::S001, context.nameSpan,
                          "未知命令 " + std::string(name), {}};
    }

    if (args.size() < info->required ||
        (!info->variadic && args.size() > info->maxFixed)) {
        return Diagnostic{ErrorCode::S002, context.nameSpan,
                          "参数个数不符", {}};
    }

    if (asciiEqualsIgnoreCase(name, "echo")) {
        std::string text;
        for (std::size_t index = 0; index < args.size(); ++index) {
            if (index != 0) {
                text.push_back(' ');
            }
            text += formatValue(args[index]);
        }
        text.push_back('\n');
        output.write(text);
        return std::nullopt;
    }

    CallablePtr callable = findCallable(name);
    if (!callable) {
        return Diagnostic{ErrorCode::S001, context.nameSpan,
                          "未知命令 " + std::string(name), {}};
    }

    CallContext callContext;
    callContext.calleeSpan = context.nameSpan;
    callContext.lparenSpan = context.nameSpan;
    callContext.argSpans = context.argSpans;

    CallResult result = callable->call(args, callContext);
    if (result.diagnostic) {
        return result.diagnostic;
    }
    return std::nullopt;
}

} // namespace lush::core

#include "lush/core/value.h"

#include <utility>

namespace lush::core {

std::string_view valueTypeName(ValueType type) noexcept {
    switch (type) {
    case ValueType::Int: return "int";
    case ValueType::Double: return "double";
    case ValueType::Bool: return "bool";
    case ValueType::String: return "string";
    case ValueType::List: return "list";
    case ValueType::Point: return "point";
    case ValueType::Callable: return "type";
    case ValueType::TypeToken: return "callable";
    }
    return "unknown";
}

ListValue::ListValue() : items_(std::make_shared<std::vector<Value>>()) {}

ListValue::ListValue(std::vector<Value> items)
    : items_(std::make_shared<std::vector<Value>>(std::move(items))) {}

ListValue::ListValue(const ListValue& other)
    : items_(std::make_shared<std::vector<Value>>(*other.items_)) {}

ListValue::ListValue(ListValue&& other) noexcept : items_(std::move(other.items_)) {
    if (!items_) {
        items_ = std::make_shared<std::vector<Value>>();
    }
}

ListValue& ListValue::operator=(const ListValue& other) {
    if (this == &other) {
        return *this;
    }
    items_ = std::make_shared<std::vector<Value>>(*other.items_);
    return *this;
}

ListValue& ListValue::operator=(ListValue&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    items_ = std::move(other.items_);
    if (!items_) {
        items_ = std::make_shared<std::vector<Value>>();
    }
    return *this;
}

ListValue::~ListValue() = default;

const std::vector<Value>& ListValue::items() const noexcept {
    return *items_;
}

std::vector<Value>& ListValue::items() noexcept {
    return *items_;
}

std::size_t ListValue::size() const noexcept {
    return items_->size();
}

bool ListValue::empty() const noexcept {
    return items_->empty();
}

Value::Value() noexcept : data_(std::int64_t{0}) {}

Value::Value(std::int64_t value) noexcept : data_(value) {}

Value::Value(double value) noexcept : data_(value) {}

Value::Value(bool value) noexcept : data_(value) {}

Value::Value(std::string value) : data_(std::move(value)) {}

Value::Value(ListValue value) : data_(std::move(value)) {}

Value::Value(PointValue value) noexcept : data_(value) {}

Value::Value(CallablePtr value) noexcept : data_(std::move(value)) {}

Value::Value(TypeToken value) noexcept : data_(value) {}

Value Value::makeInt(std::int64_t value) noexcept {
    return Value(value);
}

Value Value::makeDouble(double value) noexcept {
    return Value(value);
}

Value Value::makeBool(bool value) noexcept {
    return Value(value);
}

Value Value::makeString(std::string value) {
    return Value(std::move(value));
}

Value Value::makeList(ListValue value) {
    return Value(std::move(value));
}

Value Value::makePoint(PointValue value) noexcept {
    return Value(value);
}

Value Value::makeCallable(CallablePtr value) noexcept {
    return Value(std::move(value));
}

Value Value::makeTypeToken(TypeToken value) noexcept {
    return Value(value);
}

ValueType Value::type() const noexcept {
    switch (data_.index()) {
    case 0: return ValueType::Int;
    case 1: return ValueType::Double;
    case 2: return ValueType::Bool;
    case 3: return ValueType::String;
    case 4: return ValueType::List;
    case 5: return ValueType::Point;
    case 6: return ValueType::Callable;
    case 7: return ValueType::TypeToken;
    default: return ValueType::Int;
    }
}

bool Value::isInt() const noexcept {
    return std::holds_alternative<std::int64_t>(data_);
}

bool Value::isDouble() const noexcept {
    return std::holds_alternative<double>(data_);
}

bool Value::isBool() const noexcept {
    return std::holds_alternative<bool>(data_);
}

bool Value::isString() const noexcept {
    return std::holds_alternative<std::string>(data_);
}

bool Value::isList() const noexcept {
    return std::holds_alternative<ListValue>(data_);
}

bool Value::isPoint() const noexcept {
    return std::holds_alternative<PointValue>(data_);
}

bool Value::isCallable() const noexcept {
    return std::holds_alternative<CallablePtr>(data_);
}

bool Value::isTypeToken() const noexcept {
    return std::holds_alternative<TypeToken>(data_);
}

std::int64_t Value::asInt() const noexcept {
    return std::get<std::int64_t>(data_);
}

double Value::asDouble() const noexcept {
    return std::get<double>(data_);
}

bool Value::asBool() const noexcept {
    return std::get<bool>(data_);
}

const std::string& Value::asString() const noexcept {
    return std::get<std::string>(data_);
}

std::string& Value::asString() noexcept {
    return std::get<std::string>(data_);
}

const ListValue& Value::asList() const noexcept {
    return std::get<ListValue>(data_);
}

ListValue& Value::asList() noexcept {
    return std::get<ListValue>(data_);
}

const PointValue& Value::asPoint() const noexcept {
    return std::get<PointValue>(data_);
}

PointValue& Value::asPoint() noexcept {
    return std::get<PointValue>(data_);
}

CallablePtr Value::asCallable() const noexcept {
    return std::get<CallablePtr>(data_);
}

const TypeToken& Value::asTypeToken() const noexcept {
    return std::get<TypeToken>(data_);
}

bool Value::equals(const Value& other) const {
    if (isInt() && other.isInt()) {
        return asInt() == other.asInt();
    }
    if (isDouble() && other.isDouble()) {
        return asDouble() == other.asDouble();
    }
    if (isInt() && other.isDouble()) {
        return static_cast<double>(asInt()) == other.asDouble();
    }
    if (isDouble() && other.isInt()) {
        return asDouble() == static_cast<double>(other.asInt());
    }
    if (isBool() && other.isBool()) {
        return asBool() == other.asBool();
    }
    if (isString() && other.isString()) {
        return asString() == other.asString();
    }
    if (isList() && other.isList()) {
        const auto& left = asList().items();
        const auto& right = other.asList().items();
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (!left[index].equals(right[index])) {
                return false;
            }
        }
        return true;
    }
    if (isPoint() && other.isPoint()) {
        const PointValue& left = asPoint();
        const PointValue& right = other.asPoint();
        if (left.dim != right.dim) {
            return false;
        }
        for (int index = 0; index < left.dim; ++index) {
            if (left[index] != right[index]) {
                return false;
            }
        }
        return true;
    }
    if (isCallable() && other.isCallable()) {
        return asCallable() == other.asCallable();
    }
    if (isTypeToken() && other.isTypeToken()) {
        return asTypeToken() == other.asTypeToken();
    }
    return false;
}

bool Value::operator==(const Value& other) const {
    return equals(other);
}

bool Value::operator!=(const Value& other) const {
    return !equals(other);
}

CallResult CallResult::ok(Value value) {
    CallResult result;
    result.value = std::move(value);
    return result;
}

CallResult CallResult::error(Diagnostic diagnostic) {
    CallResult result;
    result.diagnostic = std::move(diagnostic);
    return result;
}

} // namespace lush::core

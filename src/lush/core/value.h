#pragma once

#include "lush/core/diagnostic.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lush::core {

enum class ValueType {
    Int,
    Double,
    Bool,
    String,
    List,
    Point,
    Callable,
};

std::string_view valueTypeName(ValueType type) noexcept;

class Value;
class Callable;

using CallablePtr = std::shared_ptr<const Callable>;

struct PointValue {
    std::array<double, 3> coordinates{};
    int dim = 2;

    double& operator[](std::size_t index) noexcept {
        return coordinates[index];
    }

    double operator[](std::size_t index) const noexcept {
        return coordinates[index];
    }
};

// ListValue owns its elements. Copying a ListValue deep-copies the element vector.
class ListValue {
public:
    ListValue();
    explicit ListValue(std::vector<Value> items);
    ListValue(const ListValue& other);
    ListValue(ListValue&& other) noexcept;
    ListValue& operator=(const ListValue& other);
    ListValue& operator=(ListValue&& other) noexcept;
    ~ListValue();

    const std::vector<Value>& items() const noexcept;
    std::vector<Value>& items() noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;

private:
    std::shared_ptr<std::vector<Value>> items_;
};

class Value {
public:
    Value() noexcept;
    explicit Value(std::int64_t value) noexcept;
    explicit Value(double value) noexcept;
    explicit Value(bool value) noexcept;
    explicit Value(std::string value);
    explicit Value(ListValue value);
    explicit Value(PointValue value) noexcept;
    explicit Value(CallablePtr value) noexcept;

    static Value makeInt(std::int64_t value) noexcept;
    static Value makeDouble(double value) noexcept;
    static Value makeBool(bool value) noexcept;
    static Value makeString(std::string value);
    static Value makeList(ListValue value);
    static Value makePoint(PointValue value) noexcept;
    static Value makeCallable(CallablePtr value) noexcept;

    ValueType type() const noexcept;

    bool isInt() const noexcept;
    bool isDouble() const noexcept;
    bool isBool() const noexcept;
    bool isString() const noexcept;
    bool isList() const noexcept;
    bool isPoint() const noexcept;
    bool isCallable() const noexcept;

    std::int64_t asInt() const noexcept;
    double asDouble() const noexcept;
    bool asBool() const noexcept;
    const std::string& asString() const noexcept;
    std::string& asString() noexcept;
    const ListValue& asList() const noexcept;
    ListValue& asList() noexcept;
    const PointValue& asPoint() const noexcept;
    PointValue& asPoint() noexcept;
    CallablePtr asCallable() const noexcept;

    // Convenience equality for tests. Incompatible types compare false; the
    // evaluator uses its own comparison path to report R002.
    bool equals(const Value& other) const;
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const;

private:
    std::variant<std::int64_t, double, bool, std::string, ListValue, PointValue, CallablePtr>
        data_;
};

struct CallContext {
    Span calleeSpan{};
    Span lparenSpan{};
    std::vector<Span> argSpans;
};

struct CallResult {
    std::optional<Value> value;
    std::optional<Diagnostic> diagnostic;

    static CallResult ok(Value value);
    static CallResult error(Diagnostic diagnostic);
};

class Callable {
public:
    virtual ~Callable() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual CallResult call(const std::vector<Value>& args,
                            const CallContext& context) const = 0;
};

} // namespace lush::core

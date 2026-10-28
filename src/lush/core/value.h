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

// 运行时值与 callable 接口。
//
// Value 使用值语义：读取/复制默认深拷贝；list 的 shared_ptr 由 ListValue
// 自定义拷贝构造克隆；callable 按身份共享。TypeToken 是类型 token（目前
// 只有 callable），不是可调用值。
namespace lush::core {

// 运行时类型标签。Callable 表示 invocable callable；TypeToken 表示类型
// token 本身，两者在用户诊断中都显示为 callable。
enum class ValueType {
    Int,
    Double,
    Bool,
    String,
    List,
    Point,
    Callable,
    TypeToken,
};

// 类型 token 的种类。保留为枚举，方便未来扩展其他类型 token。
enum class TypeTokenKind {
    Callable,
};

// 类型 token 值。相等性按 kind 比较。
struct TypeToken {
    TypeTokenKind kind = TypeTokenKind::Callable;

    bool operator==(const TypeToken&) const = default;
};

std::string_view valueTypeName(ValueType type) noexcept;

class Value;

// 返回运行期诊断中使用的类型名。
//
// 与 valueTypeName 的区别：invocable callable 和 callable type token 都
// 统一显示为 "callable"，避免用户看到内部 ValueType::Callable/"type"
// 这类实现细节。参数 value 必须处于有效状态。
std::string runtimeTypeName(const Value& value);
class Callable;

using CallablePtr = std::shared_ptr<const Callable>;

// 二维/三维 point；dim 必须为 2 或 3，坐标统一存为 double。
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

// 列表值：拥有元素，拷贝时深拷贝元素 vector。
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

// 单一运行时值，variant 中按 ValueType 顺序保存具体数据。
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
    explicit Value(TypeToken value) noexcept;

    static Value makeInt(std::int64_t value) noexcept;
    static Value makeDouble(double value) noexcept;
    static Value makeBool(bool value) noexcept;
    static Value makeString(std::string value);
    static Value makeList(ListValue value);
    static Value makePoint(PointValue value) noexcept;
    static Value makeCallable(CallablePtr value) noexcept;
    static Value makeTypeToken(TypeToken value = {}) noexcept;

    ValueType type() const noexcept;

    bool isInt() const noexcept;
    bool isDouble() const noexcept;
    bool isBool() const noexcept;
    bool isString() const noexcept;
    bool isList() const noexcept;
    bool isPoint() const noexcept;
    bool isCallable() const noexcept;
    bool isTypeToken() const noexcept;

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
    const TypeToken& asTypeToken() const noexcept;

    // 测试用便捷相等：不兼容类型返回 false；求值器有独立比较路径以报 R002。
    bool equals(const Value& other) const;
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const;

private:
    std::variant<std::int64_t, double, bool, std::string, ListValue, PointValue,
                 CallablePtr, TypeToken>
        data_;
};

// 一次 callable 调用的源码位置；值调用期间有效。
struct CallContext {
    Span calleeSpan{};
    Span lparenSpan{};
    std::vector<Span> argSpans;
    SourceId source = kNoSource;
};

// callable 调用结果：成功带值，失败带 R* 诊断；二选一。
struct CallResult {
    std::optional<Value> value;
    std::optional<Diagnostic> diagnostic;

    static CallResult ok(Value value);
    static CallResult error(Diagnostic diagnostic);
};

// 可调用值接口。按身份比较；实现必须可安全共享。
class Callable {
public:
    virtual ~Callable() = default;

    // 函数/构造器显示名，用于 string(f)。
    virtual std::string_view name() const noexcept = 0;

    // 执行调用；参数已在调用前从左到右求值。
    virtual CallResult call(const std::vector<Value>& args,
                            const CallContext& context) const = 0;
};

} // namespace lush::core

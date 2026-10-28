#pragma once

#include "lush/core/value.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lush::command {

// 宿主参数类型；运行时由 bindParam 转换，不退化为隐式任意转换。
enum class ParamType {
    Int,
    Number,
    Point,
    String,
    Bool,
    List,
    Any,
};

enum class ReturnKind {
    Void,
    Value,
};

// Completion-oriented static return metadata. It never enforces the runtime
// value returned by a host function.
struct ReturnType {
    core::ValueType type = core::ValueType::Int;
    int dim = 0; // only meaningful for Point; 0 means unknown/unspecified
};

// 单个宿主命令参数；optional/default/variadic 的合法性在注册时检查。
struct ParamSpec {
    std::string name;
    ParamType type = ParamType::Any;
    int dim = 0;
    bool optional = false;
    std::optional<core::Value> defaultValue;
    bool variadic = false;
    std::string hint;
};

// 宿主注册命令的输入描述；注册后转换为 Signature。
struct CommandSpec {
    std::string name;
    std::vector<std::string> aliases;
    std::vector<ParamSpec> params;
    ReturnKind returns = ReturnKind::Void;
    std::optional<ReturnType> returnType;
    bool interactive = false;
    std::string help;
};

// 注册时校验后的不可变签名；默认值已完成类型绑定。
struct Signature {
    std::string name;
    std::vector<std::string> aliases;
    std::vector<ParamSpec> params;
    std::string help;
    std::size_t required = 0;
    std::size_t maxFixed = 0;
    bool variadic = false;
    bool interactive = false;
    bool returnsValue = false;
    std::optional<ReturnType> returnType;
};

} // namespace lush::command

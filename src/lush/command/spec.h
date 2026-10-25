#pragma once

#include "lush/core/value.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lush::command {

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

struct ParamSpec {
    std::string name;
    ParamType type = ParamType::Any;
    int dim = 0;
    bool optional = false;
    std::optional<core::Value> defaultValue;
    bool variadic = false;
    std::string hint;
};

struct CommandSpec {
    std::string name;
    std::vector<std::string> aliases;
    std::vector<ParamSpec> params;
    ReturnKind returns = ReturnKind::Void;
    std::optional<ReturnType> returnType;
    bool interactive = false;
    std::string help;
};

// A CommandSpec after registration-time validation.  Defaults are already
// bound to the declared parameter types.
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

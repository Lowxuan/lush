#pragma once

#include "lush/core/ast.h"
#include "lush/core/cell.h"
#include "lush/core/diagnostic.h"
#include "lush/core/output.h"

#include <optional>

// 树遍历求值器。
//
// 求值前提：程序已通过 validate；这里产生 R* 运行时诊断。Env/Output/
// NameCatalog 只在 eval 调用期间有效，求值器不持有它们的所有权。
namespace lush::core {

class NameCatalog;

struct EvalContext {
    Env& env;
    Output& output;
    const NameCatalog& names;
    SourceId source = kNoSource; // 当前源文件；kNoSource 表示无文本来源
};

struct EvalResult {
    std::optional<Diagnostic> diagnostic;

    bool ok() const noexcept {
        return !diagnostic.has_value();
    }
};

struct EvalExpressionResult {
    std::optional<Value> value;
    std::optional<Diagnostic> diagnostic;

    bool ok() const noexcept {
        return !diagnostic.has_value();
    }
};

// 执行整段已校验程序；遇到首个运行时错误即停止。
EvalResult evalProgram(const Program& program, EvalContext& context);

// 求值单个已校验表达式，不执行语句。
EvalExpressionResult evalExpression(const Expr& expression,
                                    EvalContext& context);

} // namespace lush::core

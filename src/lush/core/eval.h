#pragma once

#include "lush/core/ast.h"
#include "lush/core/cell.h"
#include "lush/core/diagnostic.h"
#include "lush/core/output.h"

#include <optional>

namespace lush::core {

class NameCatalog;

struct EvalContext {
    Env& env;
    Output& output;
    const NameCatalog& names;
    SourceId source = kNoSource;
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

EvalResult evalProgram(const Program& program, EvalContext& context);
EvalExpressionResult evalExpression(const Expr& expression,
                                    EvalContext& context);

} // namespace lush::core

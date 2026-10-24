#pragma once

#include "test_harness.h"

#include "lush/core/builtins.h"
#include "lush/core/cell.h"
#include "lush/core/eval.h"
#include "lush/core/lexer.h"
#include "lush/core/parser.h"
#include "lush/core/source.h"
#include "lush/core/validate.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lush::test {

using lush::core::BuiltinCatalog;
using lush::core::Cell;
using lush::core::Diagnostic;
using lush::core::Env;
using lush::core::ErrorCode;
using lush::core::EvalContext;
using lush::core::EvalResult;
using lush::core::LexResult;
using lush::core::Lexer;
using lush::core::NameCatalog;
using lush::core::Output;
using lush::core::ParseOptions;
using lush::core::ParseResult;
using lush::core::ParseStatus;
using lush::core::Source;
using lush::core::ValidationOptions;
using lush::core::ValidationResult;
using lush::core::Value;
using lush::core::evalProgram;
using lush::core::parse;
using lush::core::validate;

class StringOutput final : public Output {
public:
    void write(std::string_view text) override {
        text_.append(text.data(), text.size());
    }

    const std::string& text() const noexcept {
        return text_;
    }

private:
    std::string text_;
};

struct ScriptResult {
    Source source;
    std::string output;
    std::optional<Diagnostic> diagnostic;
};

inline ScriptResult runScriptWithCatalog(
    Env& env, std::string text, const NameCatalog& catalog,
    ValidationOptions options = {}) {
    Source source(std::move(text));
    Lexer lexer(source);
    LexResult lex = lexer.tokenize();
    if (lex.diagnostic) {
        return ScriptResult{std::move(source), {}, std::move(lex.diagnostic)};
    }

    ParseResult parsed = parse(lex, ParseOptions::Script);
    if (parsed.status != ParseStatus::Ok || !parsed.program) {
        return ScriptResult{std::move(source), {}, std::move(parsed.diagnostic)};
    }

    ValidationResult validation =
        validate(*parsed.program, catalog, options);
    if (validation.diagnostic) {
        return ScriptResult{std::move(source), {},
                            std::move(validation.diagnostic)};
    }

    StringOutput output;
    EvalContext context{env, output, catalog};
    EvalResult evaluated = evalProgram(*parsed.program, context);
    return ScriptResult{std::move(source), output.text(),
                        std::move(evaluated.diagnostic)};
}

inline ScriptResult runScript(Env& env, std::string text) {
    BuiltinCatalog catalog;
    return runScriptWithCatalog(env, std::move(text), catalog);
}

inline void expectOutput(const ScriptResult& result,
                         const std::string& expected) {
    LUSH_CHECK(!result.diagnostic.has_value());
    if (result.diagnostic) {
        return;
    }
    LUSH_CHECK_EQ(result.output, expected);
}

inline void expectNoError(const ScriptResult& result) {
    LUSH_CHECK(!result.diagnostic.has_value());
}

inline void expectErrorCode(const ScriptResult& result, ErrorCode code) {
    LUSH_CHECK(result.diagnostic.has_value());
    if (!result.diagnostic) {
        return;
    }
    LUSH_CHECK_EQ(static_cast<int>(result.diagnostic->code),
                  static_cast<int>(code));
}

inline void expectError(const ScriptResult& result, ErrorCode code,
                        std::size_t line, std::size_t column) {
    LUSH_CHECK(result.diagnostic.has_value());
    if (!result.diagnostic) {
        return;
    }

    LUSH_CHECK_EQ(static_cast<int>(result.diagnostic->code),
                  static_cast<int>(code));
    const lush::core::LineCol position =
        result.source.lineCol(result.diagnostic->span.start);
    LUSH_CHECK_EQ(position.line, line);
    LUSH_CHECK_EQ(position.column, column);
}

inline std::shared_ptr<Cell> findCell(Env& env, std::string_view name) {
    return env.find(name);
}

inline bool hasVariable(Env& env, std::string_view name) {
    return env.find(name) != nullptr;
}

inline std::int64_t intValue(Env& env, std::string_view name) {
    auto cell = env.find(name);
    if (!cell) {
        LUSH_CHECK(false);
        return 0;
    }
    return cell->read().asInt();
}

} // namespace lush::test

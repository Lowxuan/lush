#pragma once

#include "lush/core/ast.h"
#include "lush/core/diagnostic.h"
#include "lush/core/lexer.h"
#include "lush/core/source.h"
#include "lush/core/token.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace lush::core {

enum class ParseOptions {
    Script,
    Repl,
};

enum class ParseStatus {
    Ok,
    Error,
    Incomplete,
};

enum class IncompleteReason {
    UnclosedBrace,
    UnclosedParen,
    UnclosedBracket,
    TrailingContinuation,
    NeedStatement,
};

struct ParseResult {
    ParseStatus status = ParseStatus::Error;
    std::optional<IncompleteReason> incompleteReason;
    std::unique_ptr<Program> program;
    std::optional<Diagnostic> diagnostic;
};

struct ExprParseResult {
    ParseStatus status = ParseStatus::Error;
    std::optional<IncompleteReason> incompleteReason;
    std::unique_ptr<Expr> expression;
    std::optional<Diagnostic> diagnostic;
};

struct TokenRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

enum class WordEnd {
    WordBoundary,
    StatementTerminator,
    InputEnd,
};

enum class Boundary {
    WordBoundary,
    StatementTerminator,
    InputEnd,
    ClosingParen,
    ClosingBracket,
    Comma,
    Colon,
};

struct CommandWord {
    TokenRange range;
    WordEnd end = WordEnd::WordBoundary;
};

ParseResult parse(const LexResult& lex, ParseOptions options = ParseOptions::Script);

ExprParseResult parseExpression(const LexResult& lex, TokenRange range,
                                Boundary boundary,
                                ParseOptions options = ParseOptions::Script);

ExprParseResult parseWord(const LexResult& lex, TokenRange range, WordEnd end,
                          ParseOptions options = ParseOptions::Script);

std::vector<CommandWord> splitCommandWords(const std::vector<Token>& tokens,
                                           std::size_t begin, std::size_t end);

} // namespace lush::core

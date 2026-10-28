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

// 递归下降解析器 + 优先级爬升。
//
// 解析阶段只建立 AST 并报告 P* 诊断；名称/类型检查留给 validate。
// Incomplete 不是错误，仅供 REPL 判断是否继续收集输入。
namespace lush::core {

enum class ParseOptions {
    Script, // 脚本：EOF 时未闭合结构按 P002 处理
    Repl,   // REPL：未闭合结构/行尾续行返回 Incomplete
};

enum class ParseStatus {
    Ok,
    Error,
    Incomplete,
};

// REPL 继续收集输入的原因；NeedStatement 也用于 def/for 等需要 body 的位置。
enum class IncompleteReason {
    UnclosedBrace,
    UnclosedParen,
    UnclosedBracket,
    TrailingContinuation,
    NeedStatement,
};

// parse() 的结果：成功给 program，失败给 diagnostic，不完整给 reason。
struct ParseResult {
    ParseStatus status = ParseStatus::Error;
    std::optional<IncompleteReason> incompleteReason;
    std::unique_ptr<Program> program;
    std::optional<Diagnostic> diagnostic;
};

// 表达式/命令词子解析的结果，结构与 ParseResult 对应。
struct ExprParseResult {
    ParseStatus status = ParseStatus::Error;
    std::optional<IncompleteReason> incompleteReason;
    std::unique_ptr<Expr> expression;
    std::optional<Diagnostic> diagnostic;
};

// token 下标区间 [begin, end)。
struct TokenRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// 命令参数词的结束原因，影响 P004/P002 的错误位置和提示。
enum class WordEnd {
    WordBoundary,
    StatementTerminator,
    InputEnd,
};

// 表达式子解析器允许停止的边界；与词边界不同，它描述调用方语法位置。
enum class Boundary {
    WordBoundary,
    StatementTerminator,
    InputEnd,
    ClosingParen,
    ClosingBracket,
    Comma,
    Colon,
};

// 命令按顶层空白切出的一个词，range 只覆盖词内 token。
struct CommandWord {
    TokenRange range;
    WordEnd end = WordEnd::WordBoundary;
};

// 解析完整程序。参数 options 决定 EOF 处未闭合结构是错误还是 Incomplete。
ParseResult parse(const LexResult& lex,
                  ParseOptions options = ParseOptions::Script);

// 在指定 token 区间和边界内解析表达式；调用方负责提供正确的边界。
ExprParseResult parseExpression(const LexResult& lex, TokenRange range,
                                Boundary boundary,
                                ParseOptions options = ParseOptions::Script);

// 在命令参数位置解析一个词；word 内允许裸 point 等命令专用语法。
ExprParseResult parseWord(const LexResult& lex, TokenRange range, WordEnd end,
                          ParseOptions options = ParseOptions::Script);

// 按顶层空白切命令参数。括号/方括号/花括号内的空白不切分。
std::vector<CommandWord> splitCommandWords(const std::vector<Token>& tokens,
                                           std::size_t begin, std::size_t end);

} // namespace lush::core

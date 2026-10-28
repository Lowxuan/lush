#pragma once

#include "lush/core/diagnostic.h"
#include "lush/core/source.h"
#include "lush/core/token.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// 词法分析器。
//
// 严格模式：遇到第一个非法 token 即记录诊断并停止。Lexer 只处理字符
// 层面（注释、续行、字符串/数字、关键字、运算符），不判断语句是否完整。
namespace lush::core {

// 一次词法分析的全部输出。
//
// comments 保存注释的原始 Span，但不把注释变成 Token；Parser 需要它来
// 区分 def 头后的“空行”与“纯注释行”。tokens 以 NL/EOF 结尾。
struct LexResult {
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    std::vector<Token> tokens;
    std::vector<Span> comments;
    std::optional<Diagnostic> diagnostic;
    bool endsWithContinuation = false;
    std::size_t lastContinuationOffset = npos;
    std::size_t endOffset = 0;
};

// 持有 Source 的只读引用；Source 必须活到 tokenize() 返回之后。
class Lexer {
public:
    explicit Lexer(const Source& source);

    // 执行一次完整词法分析；不会修改 Source。
    LexResult tokenize();

private:
    void buildLogicalText();

    bool atEnd() const noexcept;
    char charAt(std::size_t offset) const noexcept;
    Span spanOf(std::size_t start, std::size_t end) const noexcept;
    std::string textOf(std::size_t start, std::size_t end) const;
    bool wsBeforeAt(std::size_t offset) const noexcept;

    void emit(TokenKind kind, std::size_t start, std::size_t end, bool wsBefore,
              std::string text);
    void fail(ErrorCode code, std::size_t start, std::size_t end, std::string message,
              std::string help = {});

    void scanComment();
    void scanNewline();
    void scanIdentifier();
    void scanNumber();
    void scanString();
    void scanBackslash();
    void scanIllegal();
    bool scanOperator();

    const Source& source_;
    std::string logical_;
    std::vector<std::size_t> map_;
    std::vector<Token> tokens_;
    std::vector<Span> comments_;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    std::size_t lastContinuationOffset_ = LexResult::npos;
    std::size_t lastContinuationLogicalPos_ = LexResult::npos;
    bool seenNonNlAfterLastContinuation_ = false;
    std::optional<Diagnostic> diagnostic_;
};

} // namespace lush::core

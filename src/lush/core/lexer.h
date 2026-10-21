#pragma once

#include "lush/core/diagnostic.h"
#include "lush/core/source.h"
#include "lush/core/token.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lush::core {

struct LexResult {
    std::vector<Token> tokens;
    std::optional<Diagnostic> diagnostic;
};

class Lexer {
public:
    explicit Lexer(const Source& source);

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
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    std::optional<Diagnostic> diagnostic_;
};

} // namespace lush::core

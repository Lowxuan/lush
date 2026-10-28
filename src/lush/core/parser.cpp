#include "lush/core/parser.h"

#include "lush/core/ast.h"
#include "lush/core/diagnostic.h"
#include "lush/core/lexer.h"
#include "lush/core/token.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lush::core {
namespace {

enum class DelimiterKind {
    Brace,
    Paren,
    Bracket,
};

struct Delimiter {
    DelimiterKind kind;
    Span span;
};

bool isTerminator(TokenKind kind) noexcept {
    return kind == TokenKind::Newline || kind == TokenKind::Semicolon ||
           kind == TokenKind::RightBrace || kind == TokenKind::KwElse ||
           kind == TokenKind::EndOfFile;
}

bool isCloser(TokenKind kind) noexcept {
    return kind == TokenKind::RightParen || kind == TokenKind::RightBracket ||
           kind == TokenKind::RightBrace;
}

bool isStatementStarter(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::Identifier:
    case TokenKind::LeftBrace:
    case TokenKind::Star:
    case TokenKind::KwIf:
    case TokenKind::KwWhile:
    case TokenKind::KwFor:
    case TokenKind::KwBreak:
    case TokenKind::KwContinue:
    case TokenKind::KwDef:
    case TokenKind::KwReturn:
        return true;
    default:
        return false;
    }
}

bool startsExpression(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::Int:
    case TokenKind::BoundaryInt:
    case TokenKind::Float:
    case TokenKind::String:
    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
    case TokenKind::Identifier:
    case TokenKind::LeftParen:
    case TokenKind::LeftBracket:
    case TokenKind::Minus:
    case TokenKind::Bang:
    case TokenKind::PlusPlus:
    case TokenKind::MinusMinus:
    case TokenKind::Star:
        return true;
    default:
        return false;
    }
}

Span spanOfToken(const Token& token) noexcept {
    return Span{token.offset, token.offset + token.length};
}

Span mergeSpan(Span left, Span right) noexcept {
    return Span{left.start, right.end};
}

bool isAssignable(const Expr& expression) {
    return dynamic_cast<const Variable*>(&expression) != nullptr ||
           dynamic_cast<const Index*>(&expression) != nullptr ||
           dynamic_cast<const Member*>(&expression) != nullptr;
}

std::optional<Diagnostic> validateBoundary(const Program& program);
std::optional<Diagnostic> validateBoundary(const Expr& expression);

ExprParseResult parseWordRange(const LexResult& lex, TokenRange range, WordEnd end,
                               ParseOptions options, bool handleLexDiagnostic);
ExprParseResult parseExpressionRange(const LexResult& lex, TokenRange range,
                                     Boundary boundary, ParseOptions options,
                                     bool handleLexDiagnostic);

class BoundaryValidator {
public:
    void program(const Program& node) {
        for (const StmtPtr& statement : node.statements) {
            stmt(statement.get());
        }
    }

    void stmt(const Stmt* node) {
        if (node == nullptr) {
            return;
        }

        if (const auto* block = dynamic_cast<const Block*>(node)) {
            for (const StmtPtr& statement : block->statements) {
                stmt(statement.get());
            }
        } else if (const auto* ifStmt = dynamic_cast<const If*>(node)) {
            expr(ifStmt->cond.get(), false);
            stmt(ifStmt->thenBranch.get());
            stmt(ifStmt->elseBranch.get());
        } else if (const auto* whileStmt = dynamic_cast<const While*>(node)) {
            expr(whileStmt->cond.get(), false);
            stmt(whileStmt->body.get());
        } else if (const auto* forIn = dynamic_cast<const ForIn*>(node)) {
            expr(forIn->iterable.get(), false);
            stmt(forIn->body.get());
        } else if (const auto* forStmt = dynamic_cast<const For*>(node)) {
            if (forStmt->init) {
                stmt(forStmt->init.get());
            }
            expr(forStmt->cond.get(), false);
            expr(forStmt->step.get(), false);
            stmt(forStmt->body.get());
        } else if (const auto* assign = dynamic_cast<const Assign*>(node)) {
            expr(assign->target.get(), false);
            expr(assign->value.get(), false);
        } else if (const auto* command = dynamic_cast<const Command*>(node)) {
            for (const ExprPtr& arg : command->args) {
                expr(arg.get(), false);
            }
        } else if (const auto* def = dynamic_cast<const Def*>(node)) {
            stmt(def->body.get());
        } else if (const auto* ret = dynamic_cast<const Return*>(node)) {
            expr(ret->value.get(), false);
        }
    }

    void expr(const Expr* node, bool allowBoundary) {
        if (node == nullptr) {
            return;
        }

        if (dynamic_cast<const BoundaryInt*>(node) != nullptr) {
            if (!allowBoundary) {
                invalid(node->span);
            }
            return;
        }

        if (const auto* unary = dynamic_cast<const Unary*>(node)) {
            expr(unary->operand.get(), unary->op == UnaryOp::Negate);
        } else if (const auto* binary = dynamic_cast<const Binary*>(node)) {
            expr(binary->left.get(), false);
            expr(binary->right.get(), false);
        } else if (const auto* ternary = dynamic_cast<const Ternary*>(node)) {
            expr(ternary->cond.get(), false);
            expr(ternary->thenExpr.get(), false);
            expr(ternary->elseExpr.get(), false);
        } else if (const auto* call = dynamic_cast<const Call*>(node)) {
            expr(call->callee.get(), false);
            for (const ExprPtr& arg : call->args) {
                expr(arg.get(), false);
            }
        } else if (const auto* index = dynamic_cast<const Index*>(node)) {
            expr(index->receiver.get(), false);
            expr(index->index.get(), false);
        } else if (const auto* member = dynamic_cast<const Member*>(node)) {
            expr(member->receiver.get(), false);
        } else if (const auto* incDec = dynamic_cast<const IncDec*>(node)) {
            expr(incDec->target.get(), false);
        } else if (const auto* deref = dynamic_cast<const Deref*>(node)) {
            expr(deref->variable.get(), false);
        } else if (const auto* point = dynamic_cast<const Point*>(node)) {
            for (const ExprPtr& coordinate : point->coordinates) {
                expr(coordinate.get(), false);
            }
        } else if (const auto* list = dynamic_cast<const List*>(node)) {
            for (const ExprPtr& element : list->elements) {
                expr(element.get(), false);
            }
        }
    }

    std::optional<Diagnostic> diagnostic;

private:
    void invalid(Span span) {
        if (!diagnostic || span.start < diagnostic->span.start) {
            diagnostic = Diagnostic{ErrorCode::L004, span, "整数越界", {}};
        }
    }
};

std::optional<Diagnostic> validateBoundary(const Program& program) {
    BoundaryValidator validator;
    validator.program(program);
    return validator.diagnostic;
}

std::optional<Diagnostic> validateBoundary(const Expr& expression) {
    BoundaryValidator validator;
    validator.expr(&expression, false);
    return validator.diagnostic;
}

class BoundaryNormalizer {
public:
    void program(const Program& node) {
        for (const StmtPtr& statement : node.statements) {
            stmt(statement.get());
        }
    }

    void stmt(const Stmt* node) {
        if (node == nullptr) {
            return;
        }
        if (const auto* block = dynamic_cast<const Block*>(node)) {
            for (const StmtPtr& statement : block->statements) {
                stmt(statement.get());
            }
        } else if (const auto* ifStmt = dynamic_cast<const If*>(node)) {
            expr(ifStmt->cond.get());
            stmt(ifStmt->thenBranch.get());
            stmt(ifStmt->elseBranch.get());
        } else if (const auto* whileStmt = dynamic_cast<const While*>(node)) {
            expr(whileStmt->cond.get());
            stmt(whileStmt->body.get());
        } else if (const auto* forIn = dynamic_cast<const ForIn*>(node)) {
            expr(forIn->iterable.get());
            stmt(forIn->body.get());
        } else if (const auto* forStmt = dynamic_cast<const For*>(node)) {
            if (forStmt->init) {
                stmt(forStmt->init.get());
            }
            expr(forStmt->cond.get());
            expr(forStmt->step.get());
            stmt(forStmt->body.get());
        } else if (const auto* assign = dynamic_cast<const Assign*>(node)) {
            expr(assign->target.get());
            expr(assign->value.get());
        } else if (const auto* command = dynamic_cast<const Command*>(node)) {
            for (const ExprPtr& arg : command->args) {
                expr(arg.get());
            }
        } else if (const auto* def = dynamic_cast<const Def*>(node)) {
            stmt(def->body.get());
        } else if (const auto* ret = dynamic_cast<const Return*>(node)) {
            expr(ret->value.get());
        }
    }

    void expr(const Expr* node) {
        if (node == nullptr) {
            return;
        }
        if (auto* boundary = dynamic_cast<const BoundaryInt*>(node)) {
            const_cast<BoundaryInt*>(boundary)->span = boundary->literalSpan;
            return;
        }
        if (const auto* unary = dynamic_cast<const Unary*>(node)) {
            expr(unary->operand.get());
        } else if (const auto* binary = dynamic_cast<const Binary*>(node)) {
            expr(binary->left.get());
            expr(binary->right.get());
        } else if (const auto* ternary = dynamic_cast<const Ternary*>(node)) {
            expr(ternary->cond.get());
            expr(ternary->thenExpr.get());
            expr(ternary->elseExpr.get());
        } else if (const auto* call = dynamic_cast<const Call*>(node)) {
            expr(call->callee.get());
            for (const ExprPtr& arg : call->args) {
                expr(arg.get());
            }
        } else if (const auto* index = dynamic_cast<const Index*>(node)) {
            expr(index->receiver.get());
            expr(index->index.get());
        } else if (const auto* member = dynamic_cast<const Member*>(node)) {
            expr(member->receiver.get());
        } else if (const auto* incDec = dynamic_cast<const IncDec*>(node)) {
            expr(incDec->target.get());
        } else if (const auto* deref = dynamic_cast<const Deref*>(node)) {
            expr(deref->variable.get());
        } else if (const auto* point = dynamic_cast<const Point*>(node)) {
            for (const ExprPtr& coordinate : point->coordinates) {
                expr(coordinate.get());
            }
        } else if (const auto* list = dynamic_cast<const List*>(node)) {
            for (const ExprPtr& element : list->elements) {
                expr(element.get());
            }
        }
    }
};

void normalizeBoundarySpans(const Program& program) {
    BoundaryNormalizer normalizer;
    normalizer.program(program);
}

void normalizeBoundarySpans(const Expr& expression) {
    BoundaryNormalizer normalizer;
    normalizer.expr(&expression);
}

class Parser {
public:
    Parser(const LexResult& lex, std::size_t begin, std::size_t limit,
           ParseOptions options, bool inWord = false)
        : lex_(lex), tokens_(lex.tokens), options_(options), inWord_(inWord),
          limit_(std::min(limit, tokens_.size())) {
        pos_ = std::min(begin, limit_);
        endOffset_ = lex.endOffset;
        syntheticEof_.kind = TokenKind::EndOfFile;
        syntheticEof_.offset = endOffset_;
        syntheticEof_.length = 0;
        syntheticEof_.wsBefore = false;
        if (inWord_ && begin < limit_) {
            wordStartSpan_ = spanOfToken(tokens_[begin]);
        }
    }

    const std::optional<Diagnostic>& diagnostic() const noexcept { return diagnostic_; }
    bool needStatement() const noexcept { return needStatement_; }
    bool deferrableRefMissingRhs() const noexcept { return deferrableRefMissingRhs_; }
    bool parseStopped() const noexcept { return parseStopped_; }
    std::size_t position() const noexcept { return pos_; }
    std::size_t failureOffset() const noexcept {
        return pos_ < limit_ ? tokens_[pos_].offset : endOffset_;
    }

    std::optional<IncompleteReason> subIncomplete() const noexcept {
        return subIncomplete_;
    }

    bool hasUnclosed() const noexcept { return !delimiters_.empty(); }

    std::optional<Span> incompleteMemberDot() const noexcept {
        return incompleteMemberDot_;
    }

    IncompleteReason innermostUnclosed() const noexcept {
        if (delimiters_.empty()) {
            return IncompleteReason::UnclosedBrace;
        }
        switch (delimiters_.back().kind) {
        case DelimiterKind::Brace: return IncompleteReason::UnclosedBrace;
        case DelimiterKind::Paren: return IncompleteReason::UnclosedParen;
        case DelimiterKind::Bracket: return IncompleteReason::UnclosedBracket;
        }
        return IncompleteReason::UnclosedBrace;
    }

    std::unique_ptr<Program> parseProgram() {
        auto program = std::make_unique<Program>();
        skipNewlines();

        while (!atEnd()) {
            StmtPtr statement = parseStatement();
            if (!statement) {
                if (!diagnostic_) {
                    parseStopped_ = true;
                }
                break;
            }
            program->statements.push_back(std::move(statement));
            skipNewlines();
        }

        if (!program->statements.empty()) {
            program->span =
                Span{program->statements.front()->span.start,
                     program->statements.back()->span.end};
        } else {
            program->span = Span{0, 0};
        }
        return program;
    }

    ExprPtr parseExpression(bool operandExpected = false) {
        return parseTernary(operandExpected);
    }

    ExprPtr parseWordExpression() {
        if (!inWord_) {
            return nullptr;
        }
        expressionStarts_.push_back(wordStartSpan_);

        const std::size_t start = pos_;
        ExprPtr bare;
        const BareParse bareResult = parseBarePoint(bare);
        if (bareResult == BareParse::Ok) {
            return bare;
        }
        if (bareResult == BareParse::Error) {
            return nullptr;
        }

        pos_ = start;
        return parseExpression(false);
    }

    ExprPtr parseExpressionBody() { return parseExpression(false); }

private:
    enum class BareParse {
        NotBare,
        Error,
        Ok,
    };

    const Token& boundaryToken() const {
        if (limit_ < tokens_.size()) {
            return tokens_[limit_];
        }
        return syntheticEof_;
    }

    const Token& current() const {
        if (pos_ < limit_) {
            return tokens_[pos_];
        }
        return boundaryToken();
    }

    bool atEnd() const noexcept { return pos_ >= limit_; }

    bool check(TokenKind kind) const {
        return pos_ < limit_ && tokens_[pos_].kind == kind;
    }

    bool checkTight(TokenKind kind) const {
        return pos_ < limit_ && tokens_[pos_].kind == kind && !tokens_[pos_].wsBefore;
    }

    void advance() {
        if (pos_ < limit_) {
            ++pos_;
        }
    }

    void skipNewlines() {
        while (!atEnd() && check(TokenKind::Newline)) {
            advance();
        }
    }

    Span boundaryZeroSpan() const {
        const Token& token = boundaryToken();
        return Span{token.offset, token.offset};
    }

    Span exprStartSpan() const {
        if (!expressionStarts_.empty()) {
            return expressionStarts_.back();
        }
        return spanOfToken(current());
    }

    void setDiag(ErrorCode code, Span span, std::string message, std::string help = {}) {
        if (!diagnostic_) {
            diagnostic_ = Diagnostic{code, span, std::move(message), std::move(help)};
        }
    }

    void setDiagIfEarlier(ErrorCode code, Span span, std::string message,
                          std::string help = {}) {
        if (!diagnostic_ || span.start < diagnostic_->span.start) {
            diagnostic_ = Diagnostic{code, span, std::move(message), std::move(help)};
        }
    }

    bool failUnexpected() {
        setDiag(ErrorCode::P001, spanOfToken(current()), "意外的 token");
        return false;
    }

    bool failP002AtBoundary() {
        setDiag(ErrorCode::P002, boundaryZeroSpan(), "意外的结束",
                "缺少表达式或语句内容");
        return false;
    }

    bool atStatementLevel() const noexcept {
        for (const Delimiter& delimiter : delimiters_) {
            if (delimiter.kind != DelimiterKind::Brace) {
                return false;
            }
        }
        return true;
    }

    bool closerMatches(TokenKind kind) const noexcept {
        if (delimiters_.empty()) {
            return false;
        }
        const DelimiterKind top = delimiters_.back().kind;
        if (kind == TokenKind::RightParen) {
            return top == DelimiterKind::Paren;
        }
        if (kind == TokenKind::RightBracket) {
            return top == DelimiterKind::Bracket;
        }
        if (kind == TokenKind::RightBrace) {
            return top == DelimiterKind::Brace;
        }
        return false;
    }

    void pushDelimiter(DelimiterKind kind, Span span) {
        delimiters_.push_back(Delimiter{kind, span});
    }

    void popDelimiter() {
        if (!delimiters_.empty()) {
            delimiters_.pop_back();
        }
    }

    bool failExpressionExpected(bool operandExpected) {
        if (atEnd()) {
            return false;
        }

        const Token& token = current();

        if (isCloser(token.kind)) {
            if (!delimiters_.empty() && !closerMatches(token.kind)) {
                setDiag(ErrorCode::P005, spanOfToken(token), "括号不匹配");
                return false;
            }
            if (inWord_ && operandExpected) {
                setDiag(ErrorCode::P004, exprStartSpan(),
                        "命令参数不是合法的表达式");
                return false;
            }
            if (token.kind == TokenKind::RightParen ||
                token.kind == TokenKind::RightBracket) {
                setDiag(ErrorCode::P001, spanOfToken(token), "意外的 token");
                return false;
            }
            if (atStatementLevel()) {
                setDiag(ErrorCode::P002, Span{token.offset, token.offset},
                        "意外的结束", "缺少表达式");
                return false;
            }
            setDiag(ErrorCode::P005, spanOfToken(token), "括号不匹配");
            return false;
        }

        if (isTerminator(token.kind) && atStatementLevel()) {
            setDiag(ErrorCode::P002, Span{token.offset, token.offset},
                    "意外的结束", "缺少表达式");
            return false;
        }

        if (inWord_ && operandExpected) {
            setDiag(ErrorCode::P004, exprStartSpan(), "命令参数不是合法的表达式");
            return false;
        }

        return failUnexpected();
    }

    bool expect(TokenKind kind, std::string message) {
        if (check(kind)) {
            advance();
            return true;
        }
        if (atEnd()) {
            return false;
        }
        if (isCloser(current().kind) && !delimiters_.empty() &&
            !closerMatches(current().kind)) {
            setDiag(ErrorCode::P005, spanOfToken(current()), "括号不匹配");
            return false;
        }
        setDiag(ErrorCode::P001, spanOfToken(current()), std::move(message));
        return false;
    }

    bool finishStatement() {
        if (atEnd()) {
            return true;
        }
        if (check(TokenKind::Newline) || check(TokenKind::Semicolon)) {
            advance();
            return true;
        }
        if (check(TokenKind::RightBrace) || check(TokenKind::KwElse)) {
            return true;
        }

        const Token& token = current();
        if (token.kind == TokenKind::KwRef) {
            setDiag(ErrorCode::S004, spanOfToken(token), "ref 使用位置非法");
            return false;
        }
        if (isStatementStarter(token.kind)) {
            setDiag(ErrorCode::P006, spanOfToken(token), "缺少语句终止符");
            return false;
        }
        return failUnexpected();
    }

    StmtPtr parseStatement() {
        if (check(TokenKind::LeftBrace)) {
            return parseBlock();
        }
        if (check(TokenKind::KwIf)) {
            return parseIf();
        }
        if (check(TokenKind::KwWhile)) {
            return parseWhile();
        }
        if (check(TokenKind::KwFor)) {
            return parseFor();
        }
        if (check(TokenKind::KwBreak)) {
            return parseBreakLike(true);
        }
        if (check(TokenKind::KwContinue)) {
            return parseBreakLike(false);
        }
        if (check(TokenKind::KwDef)) {
            return parseDef();
        }
        if (check(TokenKind::KwReturn)) {
            return parseReturn();
        }
        if (check(TokenKind::KwRef)) {
            setDiag(ErrorCode::S004, spanOfToken(current()), "ref 使用位置非法");
            return nullptr;
        }
        if (check(TokenKind::Star)) {
            return parseStarAssign();
        }
        if (check(TokenKind::Identifier)) {
            return parseIdentifierStatement();
        }
        if (check(TokenKind::Semicolon)) {
            const Span span = spanOfToken(current());
            advance();
            auto statement = std::make_unique<EmptyStmt>();
            statement->span = span;
            return statement;
        }

        if (atEnd()) {
            return nullptr;
        }

        if (check(TokenKind::RightParen) || check(TokenKind::RightBracket)) {
            if (!delimiters_.empty()) {
                setDiag(ErrorCode::P005, spanOfToken(current()), "括号不匹配");
            } else {
                failUnexpected();
            }
            return nullptr;
        }

        failUnexpected();
        return nullptr;
    }

    bool hasCommentBetween(std::size_t begin, std::size_t end) const {
        for (const Span& comment : lex_.comments) {
            if (comment.start >= begin && comment.start < end) {
                return true;
            }
        }
        return false;
    }

    bool parseTypeAnnotation(TypeAnnotation& type) {
        if (!check(TokenKind::Identifier)) {
            if (atEnd() || isTerminator(current().kind)) {
                return failP002AtBoundary();
            }
            return failUnexpected();
        }

        const Token& token = current();
        type.text = token.text;
        type.span = spanOfToken(token);
        type.kind = typeKindFromName(token.text).value_or(TypeKind::Unknown);
        advance();
        return true;
    }

    bool parseFunctionParams(FunctionSignature& signature) {
        if (!check(TokenKind::LeftParen)) {
            if (atEnd() || isTerminator(current().kind)) {
                return failP002AtBoundary();
            }
            return failUnexpected();
        }

        const Span lparenSpan = spanOfToken(current());
        advance();
        pushDelimiter(DelimiterKind::Paren, lparenSpan);

        if (check(TokenKind::RightParen)) {
            advance();
            popDelimiter();
            return true;
        }

        while (true) {
            if (!check(TokenKind::Identifier)) {
                if (atEnd() || isTerminator(current().kind)) {
                    failP002AtBoundary();
                } else {
                    failUnexpected();
                }
                return false;
            }

            FunctionParam param;
            const Token& nameToken = current();
            param.name = Name{nameToken.text, spanOfToken(nameToken)};
            param.span = param.name.span;
            advance();

            if (check(TokenKind::Colon)) {
                advance();
                TypeAnnotation type;
                if (!parseTypeAnnotation(type)) {
                    return false;
                }
                param.span.end = type.span.end;
                param.type = std::move(type);
            }
            signature.params.push_back(std::move(param));

            if (check(TokenKind::Comma)) {
                advance();
                if (check(TokenKind::RightParen)) {
                    setDiag(ErrorCode::P001, spanOfToken(current()),
                            "函数参数列表不能以逗号结尾");
                    return false;
                }
                continue;
            }

            if (check(TokenKind::RightParen)) {
                advance();
                popDelimiter();
                return true;
            }

            if (atEnd() || isTerminator(current().kind)) {
                return failP002AtBoundary();
            }
            return failUnexpected();
        }
    }

    StmtPtr parseDef() {
        const Span defSpan = spanOfToken(current());
        advance();

        if (!check(TokenKind::Identifier)) {
            if (atEnd() || isTerminator(current().kind)) {
                failP002AtBoundary();
            } else {
                failUnexpected();
            }
            return nullptr;
        }

        auto statement = std::make_unique<Def>();
        const Token& nameToken = current();
        statement->signature.name = Name{nameToken.text, spanOfToken(nameToken)};
        statement->signature.span = statement->signature.name.span;
        advance();

        if (!parseFunctionParams(statement->signature)) {
            return nullptr;
        }

        if (check(TokenKind::Arrow)) {
            advance();
            TypeAnnotation returnType;
            if (!parseTypeAnnotation(returnType)) {
                return nullptr;
            }
            statement->signature.returnType = std::move(returnType);
        }

        if (atEnd()) {
            needStatement_ = true;
            return nullptr;
        }

        if (check(TokenKind::Newline)) {
            std::size_t previousNewlineEnd = 0;
            bool haveNewline = false;
            while (check(TokenKind::Newline)) {
                const Token& newline = current();
                if (haveNewline &&
                    !hasCommentBetween(previousNewlineEnd, newline.offset)) {
                    setDiag(ErrorCode::P002, spanOfToken(newline),
                            "def 的 body 不能由空行开始", "删除空行或使用 ;");
                    return nullptr;
                }
                previousNewlineEnd = newline.offset + newline.length;
                haveNewline = true;
                advance();
            }
            if (atEnd()) {
                needStatement_ = true;
                return nullptr;
            }
        }

        StmtPtr body = parseStatement();
        if (!body) {
            return nullptr;
        }
        statement->body = std::move(body);
        statement->span = Span{defSpan.start, statement->body->span.end};
        statement->signature.span = statement->span;
        return statement;
    }

    StmtPtr parseReturn() {
        const Span returnSpan = spanOfToken(current());
        advance();

        auto statement = std::make_unique<Return>();
        statement->span = returnSpan;

        if (!atEnd() && !check(TokenKind::Newline) &&
            !check(TokenKind::Semicolon) && !check(TokenKind::RightBrace) &&
            !check(TokenKind::KwElse)) {
            ExprPtr value = parseExpression(false);
            if (!value) {
                return nullptr;
            }
            statement->span.end = value->span.end;
            statement->value = std::move(value);
        }

        if (!finishStatement()) {
            return nullptr;
        }
        return statement;
    }

    StmtPtr parseBlock() {
        const Span openSpan = spanOfToken(current());
        advance();
        pushDelimiter(DelimiterKind::Brace, openSpan);
        skipNewlines();

        auto block = std::make_unique<Block>();

        while (!atEnd()) {
            if (check(TokenKind::RightBrace)) {
                if (!closerMatches(TokenKind::RightBrace)) {
                    setDiag(ErrorCode::P005, spanOfToken(current()), "括号不匹配");
                    return nullptr;
                }
                const Span closeSpan = spanOfToken(current());
                advance();
                popDelimiter();
                block->span = Span{openSpan.start, closeSpan.end};
                return block;
            }

            StmtPtr statement = parseStatement();
            if (!statement) {
                return nullptr;
            }
            block->statements.push_back(std::move(statement));
            skipNewlines();
        }

        return nullptr;
    }

    StmtPtr parseBreakLike(bool isBreak) {
        const Span span = spanOfToken(current());
        advance();
        if (!finishStatement()) {
            return nullptr;
        }
        if (isBreak) {
            auto statement = std::make_unique<Break>();
            statement->span = span;
            return statement;
        }
        auto statement = std::make_unique<Continue>();
        statement->span = span;
        return statement;
    }

    bool parseControlCondition(ExprPtr& condition, Span& lparenSpan, Span& rparenSpan) {
        if (!check(TokenKind::LeftParen)) {
            if (atEnd()) {
                return false;
            }
            failUnexpected();
            return false;
        }

        lparenSpan = spanOfToken(current());
        advance();
        pushDelimiter(DelimiterKind::Paren, lparenSpan);
        expressionStarts_.push_back(lparenSpan);

        condition = parseExpression(false);
        if (!condition) {
            return false;
        }

        if (!check(TokenKind::RightParen)) {
            if (atEnd()) {
                return false;
            }
            if (isCloser(current().kind) && !closerMatches(current().kind)) {
                setDiag(ErrorCode::P005, spanOfToken(current()), "括号不匹配");
                return false;
            }
            setDiag(ErrorCode::P001, spanOfToken(current()), "意外的 token");
            return false;
        }

        rparenSpan = spanOfToken(current());
        advance();
        popDelimiter();
        expressionStarts_.pop_back();
        return true;
    }

    StmtPtr parseIf() {
        const Span ifSpan = spanOfToken(current());
        advance();

        ExprPtr condition;
        Span lparenSpan;
        Span rparenSpan;
        if (!parseControlCondition(condition, lparenSpan, rparenSpan)) {
            return nullptr;
        }

        skipNewlines();
        if (atEnd()) {
            needStatement_ = true;
            return nullptr;
        }
        if (check(TokenKind::RightBrace) || check(TokenKind::KwElse)) {
            failUnexpected();
            return nullptr;
        }

        StmtPtr thenBranch = parseStatement();
        if (!thenBranch) {
            return nullptr;
        }

        skipNewlines();
        StmtPtr elseBranch;
        if (check(TokenKind::KwElse)) {
            advance();
            skipNewlines();
            if (atEnd()) {
                needStatement_ = true;
                return nullptr;
            }
            if (check(TokenKind::RightBrace) || check(TokenKind::KwElse)) {
                failUnexpected();
                return nullptr;
            }
            elseBranch = parseStatement();
            if (!elseBranch) {
                return nullptr;
            }
        }

        auto statement = std::make_unique<If>();
        statement->cond = std::move(condition);
        statement->thenBranch = std::move(thenBranch);
        statement->elseBranch = std::move(elseBranch);
        statement->span =
            Span{ifSpan.start, statement->elseBranch ? statement->elseBranch->span.end
                                                      : statement->thenBranch->span.end};
        return statement;
    }

    StmtPtr parseWhile() {
        const Span whileSpan = spanOfToken(current());
        advance();

        ExprPtr condition;
        Span lparenSpan;
        Span rparenSpan;
        if (!parseControlCondition(condition, lparenSpan, rparenSpan)) {
            return nullptr;
        }

        skipNewlines();
        if (atEnd()) {
            needStatement_ = true;
            return nullptr;
        }
        if (check(TokenKind::RightBrace) || check(TokenKind::KwElse)) {
            failUnexpected();
            return nullptr;
        }

        StmtPtr body = parseStatement();
        if (!body) {
            return nullptr;
        }

        auto statement = std::make_unique<While>();
        statement->cond = std::move(condition);
        statement->body = std::move(body);
        statement->span = Span{whileSpan.start, statement->body->span.end};
        return statement;
    }

    bool parseForInit(std::unique_ptr<Assign>& init) {
        if (check(TokenKind::Semicolon)) {
            return true;
        }

        if (check(TokenKind::KwRef)) {
            setDiag(ErrorCode::S004, spanOfToken(current()), "ref 使用位置非法");
            return false;
        }
        if (!check(TokenKind::Identifier)) {
            if (atEnd()) {
                return false;
            }
            failUnexpected();
            return false;
        }

        const Span start = spanOfToken(current());
        auto variable = std::make_unique<Variable>();
        variable->name = Name{current().text, spanOfToken(current())};
        variable->span = variable->name.span;
        advance();

        if (!check(TokenKind::Assign)) {
            if (atEnd()) {
                return false;
            }
            failUnexpected();
            return false;
        }

        const Span assignSpan = spanOfToken(current());
        advance();
        ExprPtr value = parseExpression(false);
        if (!value) {
            return false;
        }

        init = std::make_unique<Assign>();
        init->target = std::move(variable);
        init->value = std::move(value);
        init->assignSpan = assignSpan;
        init->span = Span{start.start, init->value->span.end};
        return true;
    }

    bool parseForHeader(std::unique_ptr<Assign>& init, ExprPtr& cond, ExprPtr& step) {
        if (!check(TokenKind::LeftParen)) {
            if (atEnd()) {
                return false;
            }
            failUnexpected();
            return false;
        }

        const Span lparenSpan = spanOfToken(current());
        advance();
        pushDelimiter(DelimiterKind::Paren, lparenSpan);

        if (!parseForInit(init)) {
            return false;
        }

        if (!check(TokenKind::Semicolon)) {
            if (atEnd()) {
                return false;
            }
            failUnexpected();
            return false;
        }
        advance();

        if (!check(TokenKind::Semicolon)) {
            cond = parseExpression(false);
            if (!cond) {
                return false;
            }
        }

        if (!check(TokenKind::Semicolon)) {
            if (atEnd()) {
                return false;
            }
            failUnexpected();
            return false;
        }
        advance();

        if (!check(TokenKind::RightParen)) {
            step = parseExpression(false);
            if (!step) {
                return false;
            }
        }

        if (!check(TokenKind::RightParen)) {
            if (atEnd()) {
                return false;
            }
            if (isCloser(current().kind) && !closerMatches(current().kind)) {
                setDiag(ErrorCode::P005, spanOfToken(current()), "括号不匹配");
                return false;
            }
            failUnexpected();
            return false;
        }

        advance();
        popDelimiter();
        return true;
    }

    StmtPtr parseFor() {
        const Span forSpan = spanOfToken(current());
        advance();

        if (check(TokenKind::KwRef)) {
            setDiag(ErrorCode::S004, spanOfToken(current()), "ref 使用位置非法");
            return nullptr;
        }
        if (check(TokenKind::Identifier) && pos_ + 1 < limit_ &&
            tokens_[pos_ + 1].kind == TokenKind::KwIn) {
            const Span varSpan = spanOfToken(current());
            Name var{current().text, varSpan};
            advance();
            advance();

            ExprPtr iterable = parseExpression(false);
            if (!iterable) {
                return nullptr;
            }

            skipNewlines();
            if (atEnd()) {
                needStatement_ = true;
                return nullptr;
            }
            if (check(TokenKind::RightBrace) || check(TokenKind::KwElse)) {
                failUnexpected();
                return nullptr;
            }

            StmtPtr body = parseStatement();
            if (!body) {
                return nullptr;
            }

            auto statement = std::make_unique<ForIn>();
            statement->var = std::move(var);
            statement->iterable = std::move(iterable);
            statement->body = std::move(body);
            statement->span = Span{forSpan.start, statement->body->span.end};
            return statement;
        }

        if (check(TokenKind::LeftParen)) {
            std::unique_ptr<Assign> init;
            ExprPtr cond;
            ExprPtr step;
            if (!parseForHeader(init, cond, step)) {
                return nullptr;
            }

            skipNewlines();
            if (atEnd()) {
                needStatement_ = true;
                return nullptr;
            }
            if (check(TokenKind::RightBrace) || check(TokenKind::KwElse)) {
                failUnexpected();
                return nullptr;
            }

            StmtPtr body = parseStatement();
            if (!body) {
                return nullptr;
            }

            auto statement = std::make_unique<For>();
            statement->init = std::move(init);
            statement->cond = std::move(cond);
            statement->step = std::move(step);
            statement->body = std::move(body);
            statement->span = Span{forSpan.start, statement->body->span.end};
            return statement;
        }

        if (check(TokenKind::Identifier)) {
            advance();
            if (atEnd()) {
                return nullptr;
            }
            failUnexpected();
            return nullptr;
        }

        if (atEnd()) {
            return nullptr;
        }
        failUnexpected();
        return nullptr;
    }

    bool scanLvaluePath(std::size_t begin, std::size_t& end) const {
        std::size_t index = begin + 1;
        while (index < limit_) {
            const Token& token = tokens_[index];
            if (token.kind == TokenKind::LeftBracket && !token.wsBefore) {
                std::size_t depth = 0;
                std::size_t cursor = index;
                bool closed = false;
                while (cursor < limit_) {
                    if (tokens_[cursor].kind == TokenKind::LeftBracket) {
                        ++depth;
                    } else if (tokens_[cursor].kind == TokenKind::RightBracket) {
                        if (depth > 0) {
                            --depth;
                        }
                        if (depth == 0) {
                            closed = true;
                            ++cursor;
                            break;
                        }
                    }
                    ++cursor;
                }
                if (!closed) {
                    return false;
                }
                index = cursor;
                continue;
            }
            if (token.kind == TokenKind::Dot && !token.wsBefore && index + 1 < limit_ &&
                tokens_[index + 1].kind == TokenKind::Identifier &&
                !tokens_[index + 1].wsBefore) {
                index += 2;
                continue;
            }
            break;
        }
        end = index;
        return true;
    }

    ExprPtr parseLvaluePath(std::size_t& suffixCount) {
        const Token& nameToken = current();
        auto variable = std::make_unique<Variable>();
        variable->name = Name{nameToken.text, spanOfToken(nameToken)};
        variable->span = variable->name.span;
        ExprPtr expression = std::move(variable);
        advance();
        suffixCount = 0;

        while (!atEnd()) {
            if (checkTight(TokenKind::LeftBracket)) {
                const Span openSpan = spanOfToken(current());
                advance();
                pushDelimiter(DelimiterKind::Bracket, openSpan);

                ExprPtr index = parseExpression(false);
                if (!index) {
                    return nullptr;
                }

                if (!check(TokenKind::RightBracket)) {
                    if (atEnd()) {
                        return nullptr;
                    }
                    if (isCloser(current().kind) && !closerMatches(current().kind)) {
                        setDiag(ErrorCode::P005, spanOfToken(current()),
                                "括号不匹配");
                    } else {
                        failUnexpected();
                    }
                    return nullptr;
                }

                const Span closeSpan = spanOfToken(current());
                advance();
                popDelimiter();

                auto indexed = std::make_unique<Index>();
                indexed->receiver = std::move(expression);
                indexed->index = std::move(index);
                indexed->openSpan = openSpan;
                indexed->closeSpan = closeSpan;
                indexed->span = Span{indexed->receiver->span.start, closeSpan.end};
                expression = std::move(indexed);
                ++suffixCount;
                continue;
            }

            if (checkTight(TokenKind::Dot)) {
                const Span dotSpan = spanOfToken(current());
                advance();
                if (!check(TokenKind::Identifier) || current().wsBefore) {
                    if (atEnd()) {
                        return nullptr;
                    }
                    setDiag(ErrorCode::P004, dotSpan, "命令参数不是合法的表达式");
                    return nullptr;
                }
                const Token& memberToken = current();
                advance();

                auto member = std::make_unique<Member>();
                member->receiver = std::move(expression);
                member->member = Name{memberToken.text, spanOfToken(memberToken)};
                member->span = Span{member->receiver->span.start,
                                    member->member.span.end};
                expression = std::move(member);
                ++suffixCount;
                continue;
            }

            break;
        }

        return expression;
    }

    StmtPtr parseIdentifierStatement() {
        const std::size_t start = pos_;
        const Token& nameToken = current();
        const Name name{nameToken.text, spanOfToken(nameToken)};

        std::size_t pathEnd = 0;
        if (!scanLvaluePath(start, pathEnd)) {
            pos_ = start + 1;
            return parseCommand(name);
        }

        if (pathEnd < limit_ && tokens_[pathEnd].kind == TokenKind::Assign) {
            std::size_t suffixCount = 0;
            ExprPtr target = parseLvaluePath(suffixCount);
            if (!target) {
                return nullptr;
            }

            if (!check(TokenKind::Assign)) {
                failUnexpected();
                return nullptr;
            }
            const Span assignSpan = spanOfToken(current());
            advance();

            if (check(TokenKind::KwRef)) {
                const Span refSpan = spanOfToken(current());
                if (suffixCount != 0) {
                    setDiag(ErrorCode::S004, refSpan, "ref 使用位置非法");
                    return nullptr;
                }
                advance();

                if (atEnd() || check(TokenKind::Newline)) {
                    deferrableRefMissingRhs_ = true;
                    setDiag(ErrorCode::S004, refSpan, "ref 使用位置非法");
                    return nullptr;
                }

                if (!check(TokenKind::Identifier)) {
                    setDiag(ErrorCode::S004, refSpan, "ref 使用位置非法");
                    return nullptr;
                }

                const Token& sourceToken = current();
                const Name source{sourceToken.text, spanOfToken(sourceToken)};
                advance();

                if (!atEnd() && !check(TokenKind::Newline) &&
                    !check(TokenKind::Semicolon) &&
                    !check(TokenKind::RightBrace) && !check(TokenKind::KwElse)) {
                    setDiag(ErrorCode::S004, refSpan, "ref 使用位置非法");
                    return nullptr;
                }

                auto statement = std::make_unique<RefAssign>();
                statement->target = name;
                statement->source = source;
                statement->refSpan = refSpan;
                statement->span = Span{name.span.start, source.span.end};
                if (!finishStatement()) {
                    return nullptr;
                }
                return statement;
            }

            ExprPtr value = parseExpression(false);
            if (!value) {
                return nullptr;
            }

            auto statement = std::make_unique<Assign>();
            statement->target = std::move(target);
            statement->value = std::move(value);
            statement->assignSpan = assignSpan;
            statement->span = Span{statement->target->span.start,
                                   statement->value->span.end};
            if (!finishStatement()) {
                return nullptr;
            }
            return statement;
        }

        pos_ = start + 1;
        return parseCommand(name);
    }

    StmtPtr parseCommand(const Name& name) {
        if (!atEnd() && !current().wsBefore &&
            (current().kind == TokenKind::PlusPlus ||
             current().kind == TokenKind::MinusMinus)) {
            const std::size_t next = pos_ + 1;
            if (next >= limit_ || isTerminator(tokens_[next].kind)) {
                setDiag(ErrorCode::P004, spanOfToken(current()),
                        "命令参数不是合法的表达式");
                return nullptr;
            }
        }

        const std::size_t argsBegin = pos_;
        SplitResult split = splitWords(tokens_, argsBegin, limit_);
        std::vector<ExprPtr> args;

        for (const CommandWord& word : split.words) {
            ExprParseResult result = parseWordRange(lex_, word.range, word.end,
                                                     options_, false);
            if (result.status == ParseStatus::Error) {
                if (result.diagnostic) {
                    setDiag(result.diagnostic->code, result.diagnostic->span,
                            result.diagnostic->message, result.diagnostic->help);
                } else {
                    setDiag(ErrorCode::P001, word.range.begin < tokens_.size()
                                                 ? spanOfToken(tokens_[word.range.begin])
                                                 : boundaryZeroSpan(),
                            "意外的 token");
                }
                return nullptr;
            }
            if (result.status == ParseStatus::Incomplete) {
                subIncomplete_ = result.incompleteReason;
                return nullptr;
            }
            args.push_back(std::move(result.expression));
        }

        pos_ = split.stop;
        auto command = std::make_unique<Command>();
        command->name = name;
        command->span = name.span;
        if (!args.empty()) {
            command->span.end = args.back()->span.end;
        }
        command->args = std::move(args);

        if (!finishStatement()) {
            return nullptr;
        }
        return command;
    }

    StmtPtr parseStarAssign() {
        const Span starSpan = spanOfToken(current());
        advance();

        if (atEnd()) {
            failP002AtBoundary();
            return nullptr;
        }

        const Token& variableToken = current();
        if (variableToken.kind == TokenKind::KwRef) {
            setDiag(ErrorCode::S004, spanOfToken(variableToken), "ref 使用位置非法");
            return nullptr;
        }
        if (variableToken.kind != TokenKind::Identifier || variableToken.wsBefore) {
            if (isTerminator(variableToken.kind) && atStatementLevel()) {
                setDiag(ErrorCode::P002, Span{variableToken.offset, variableToken.offset},
                        "意外的结束", "缺少变量名");
            } else {
                failUnexpected();
            }
            return nullptr;
        }

        auto variable = std::make_unique<Variable>();
        variable->name = Name{variableToken.text, spanOfToken(variableToken)};
        variable->span = variable->name.span;
        advance();

        auto target = std::make_unique<Deref>();
        target->starSpan = starSpan;
        target->span = Span{starSpan.start, variable->span.end};
        target->variable = std::move(variable);

        if (!check(TokenKind::Assign)) {
            if (atEnd()) {
                failP002AtBoundary();
            } else if (isTerminator(current().kind) && atStatementLevel()) {
                setDiag(ErrorCode::P002, Span{current().offset, current().offset},
                        "意外的结束", "缺少 =");
            } else {
                failUnexpected();
            }
            return nullptr;
        }

        const Span assignSpan = spanOfToken(current());
        advance();

        if (check(TokenKind::KwRef)) {
            setDiag(ErrorCode::S004, spanOfToken(current()), "ref 使用位置非法");
            return nullptr;
        }

        ExprPtr value = parseExpression(false);
        if (!value) {
            return nullptr;
        }

        auto statement = std::make_unique<Assign>();
        statement->target = std::move(target);
        statement->value = std::move(value);
        statement->assignSpan = assignSpan;
        statement->span = Span{starSpan.start, statement->value->span.end};
        if (!finishStatement()) {
            return nullptr;
        }
        return statement;
    }

    bool parseBareAtom(ExprPtr& out) {
        if (atEnd()) {
            return false;
        }

        const Token& token = current();
        switch (token.kind) {
        case TokenKind::Int: {
            std::int64_t value = 0;
            const auto result = std::from_chars(token.text.data(),
                                                token.text.data() + token.text.size(),
                                                value);
            if (result.ec != std::errc{}) {
                setDiag(ErrorCode::L004, spanOfToken(token), "整数越界");
                return false;
            }
            auto expression = std::make_unique<Int>();
            expression->value = value;
            expression->span = spanOfToken(token);
            out = std::move(expression);
            advance();
            return true;
        }
        case TokenKind::BoundaryInt: {
            auto expression = std::make_unique<BoundaryInt>();
            expression->span = spanOfToken(token);
            expression->literalSpan = expression->span;
            out = std::move(expression);
            advance();
            return true;
        }
        case TokenKind::Float: {
            double value = 0.0;
            const auto result = std::from_chars(token.text.data(),
                                                token.text.data() + token.text.size(),
                                                value, std::chars_format::general);
            if (result.ec != std::errc{}) {
                setDiag(ErrorCode::L004, spanOfToken(token),
                        "数值字面量格式错误或越界");
                return false;
            }
            auto expression = std::make_unique<Float>();
            expression->value = value;
            expression->span = spanOfToken(token);
            out = std::move(expression);
            advance();
            return true;
        }
        case TokenKind::Identifier: {
            auto expression = std::make_unique<Variable>();
            expression->name = Name{token.text, spanOfToken(token)};
            expression->span = expression->name.span;
            out = std::move(expression);
            advance();
            return true;
        }
        case TokenKind::LeftParen:
            out = parseGroupingOrPoint();
            return out != nullptr;
        default:
            return false;
        }
    }

    bool parseBareSimple(ExprPtr& out) {
        if (check(TokenKind::Minus)) {
            const Span opSpan = spanOfToken(current());
            advance();
            ExprPtr atom;
            if (!parseBareAtom(atom)) {
                return false;
            }
            atom = parsePostfixOps(std::move(atom), false);
            if (!atom) {
                return false;
            }
            auto unary = std::make_unique<Unary>();
            unary->op = UnaryOp::Negate;
            unary->opSpan = opSpan;
            unary->operand = std::move(atom);
            unary->span = Span{opSpan.start, unary->operand->span.end};
            out = std::move(unary);
            return true;
        }

        ExprPtr atom;
        if (!parseBareAtom(atom)) {
            return false;
        }
        atom = parsePostfixOps(std::move(atom), false);
        if (!atom) {
            return false;
        }
        out = std::move(atom);
        return true;
    }

    bool isBarePointLiteral(const Expr& expression) const {
        return dynamic_cast<const Point*>(&expression) != nullptr &&
               !static_cast<const Point&>(expression).bare;
    }

    BareParse parseBarePoint(ExprPtr& out) {
        const std::size_t start = pos_;
        const Span wordStart = spanOfToken(current());

        ExprPtr first;
        if (!parseBareSimple(first)) {
            if (diagnostic_) {
                return BareParse::Error;
            }
            if (hasUnclosed()) {
                return BareParse::Error;
            }
            pos_ = start;
            return BareParse::NotBare;
        }

        if (!check(TokenKind::Comma)) {
            pos_ = start;
            return BareParse::NotBare;
        }

        if (isBarePointLiteral(*first)) {
            setDiag(ErrorCode::P004, spanOfToken(tokens_[start]),
                    "命令参数不是合法的表达式");
            return BareParse::Error;
        }

        std::vector<ExprPtr> coordinates;
        coordinates.push_back(std::move(first));

        while (check(TokenKind::Comma)) {
            if (coordinates.size() >= 3) {
                setDiag(ErrorCode::P004, wordStart,
                        "命令参数不是合法的表达式");
                return BareParse::Error;
            }

            advance();

            if (atEnd() || check(TokenKind::RightParen) ||
                check(TokenKind::RightBracket) || check(TokenKind::Comma)) {
                setDiag(ErrorCode::P004, wordStart,
                        "命令参数不是合法的表达式");
                return BareParse::Error;
            }

            ExprPtr coordinate;
            const std::size_t coordinateStart = pos_;
            if (!parseBareSimple(coordinate)) {
                if (diagnostic_) {
                    return BareParse::Error;
                }
                setDiag(ErrorCode::P004, wordStart,
                        "命令参数不是合法的表达式");
                return BareParse::Error;
            }

            if (isBarePointLiteral(*coordinate)) {
                setDiag(ErrorCode::P004, spanOfToken(tokens_[coordinateStart]),
                        "命令参数不是合法的表达式");
                return BareParse::Error;
            }

            coordinates.push_back(std::move(coordinate));
        }

        auto point = std::make_unique<Point>();
        point->bare = true;
        point->span = Span{coordinates.front()->span.start, coordinates.back()->span.end};
        point->coordinates = std::move(coordinates);
        out = std::move(point);
        return BareParse::Ok;
    }

    ExprPtr parseGroupingOrPoint() {
        const Span openSpan = spanOfToken(current());
        advance();
        pushDelimiter(DelimiterKind::Paren, openSpan);
        expressionStarts_.push_back(openSpan);

        if (check(TokenKind::RightParen)) {
            if (inWord_) {
                setDiag(ErrorCode::P004, openSpan, "命令参数不是合法的表达式");
            } else {
                setDiag(ErrorCode::P001, spanOfToken(current()), "意外的 token");
            }
            return nullptr;
        }

        ExprPtr first = parseExpression(false);
        if (!first) {
            return nullptr;
        }

        if (!check(TokenKind::Comma)) {
            if (!check(TokenKind::RightParen)) {
                if (atEnd()) {
                    return nullptr;
                }
                if (isCloser(current().kind) && !closerMatches(current().kind)) {
                    setDiag(ErrorCode::P005, spanOfToken(current()),
                            "括号不匹配");
                } else {
                    failUnexpected();
                }
                return nullptr;
            }

            const Span closeSpan = spanOfToken(current());
            advance();
            popDelimiter();
            expressionStarts_.pop_back();
            first->span = Span{openSpan.start, closeSpan.end};
            return first;
        }

        std::vector<ExprPtr> coordinates;
        coordinates.push_back(std::move(first));

        while (check(TokenKind::Comma)) {
            advance();
            if (check(TokenKind::RightParen)) {
                setDiag(ErrorCode::P001, spanOfToken(current()), "意外的 token");
                return nullptr;
            }
            if (coordinates.size() >= 3) {
                setDiag(ErrorCode::P003, openSpan, "Point 只能有 2 或 3 个坐标",
                        "是否想写 list？");
                return nullptr;
            }
            ExprPtr coordinate = parseExpression(false);
            if (!coordinate) {
                return nullptr;
            }
            coordinates.push_back(std::move(coordinate));
        }

        if (!check(TokenKind::RightParen)) {
            if (atEnd()) {
                return nullptr;
            }
            if (isCloser(current().kind) && !closerMatches(current().kind)) {
                setDiag(ErrorCode::P005, spanOfToken(current()), "括号不匹配");
            } else {
                failUnexpected();
            }
            return nullptr;
        }

        const Span closeSpan = spanOfToken(current());
        advance();
        popDelimiter();
        expressionStarts_.pop_back();

        auto point = std::make_unique<Point>();
        point->bare = false;
        point->span = Span{openSpan.start, closeSpan.end};
        point->coordinates = std::move(coordinates);
        return point;
    }

    ExprPtr parseList() {
        const Span openSpan = spanOfToken(current());
        advance();
        pushDelimiter(DelimiterKind::Bracket, openSpan);
        expressionStarts_.push_back(openSpan);

        if (check(TokenKind::RightBracket)) {
            const Span closeSpan = spanOfToken(current());
            advance();
            popDelimiter();
            expressionStarts_.pop_back();
            auto list = std::make_unique<List>();
            list->span = Span{openSpan.start, closeSpan.end};
            return list;
        }

        std::vector<ExprPtr> elements;
        while (true) {
            ExprPtr element = parseExpression(false);
            if (!element) {
                return nullptr;
            }
            elements.push_back(std::move(element));

            if (!check(TokenKind::Comma)) {
                break;
            }
            advance();
            if (check(TokenKind::RightBracket)) {
                setDiag(ErrorCode::P001, spanOfToken(current()), "意外的 token");
                return nullptr;
            }
        }

        if (!check(TokenKind::RightBracket)) {
            if (atEnd()) {
                return nullptr;
            }
            if (isCloser(current().kind) && !closerMatches(current().kind)) {
                setDiag(ErrorCode::P005, spanOfToken(current()), "括号不匹配");
            } else {
                failUnexpected();
            }
            return nullptr;
        }

        const Span closeSpan = spanOfToken(current());
        advance();
        popDelimiter();
        expressionStarts_.pop_back();

        auto list = std::make_unique<List>();
        list->span = Span{openSpan.start, closeSpan.end};
        list->elements = std::move(elements);
        return list;
    }

    ExprPtr parsePrimary(bool operandExpected) {
        if (atEnd()) {
            return nullptr;
        }

        const Token& token = current();
        switch (token.kind) {
        case TokenKind::Int: {
            std::int64_t value = 0;
            const auto result = std::from_chars(token.text.data(),
                                                token.text.data() + token.text.size(),
                                                value);
            if (result.ec != std::errc{}) {
                setDiag(ErrorCode::L004, spanOfToken(token), "整数越界");
                return nullptr;
            }
            auto expression = std::make_unique<Int>();
            expression->value = value;
            expression->span = spanOfToken(token);
            advance();
            return expression;
        }
        case TokenKind::BoundaryInt: {
            auto expression = std::make_unique<BoundaryInt>();
            expression->span = spanOfToken(token);
            expression->literalSpan = expression->span;
            advance();
            return expression;
        }
        case TokenKind::Float: {
            double value = 0.0;
            const auto result = std::from_chars(token.text.data(),
                                                token.text.data() + token.text.size(),
                                                value, std::chars_format::general);
            if (result.ec != std::errc{}) {
                setDiag(ErrorCode::L004, spanOfToken(token),
                        "数值字面量格式错误或越界");
                return nullptr;
            }
            auto expression = std::make_unique<Float>();
            expression->value = value;
            expression->span = spanOfToken(token);
            advance();
            return expression;
        }
        case TokenKind::String: {
            auto expression = std::make_unique<lush::core::String>();
            expression->value = token.text;
            expression->span = spanOfToken(token);
            advance();
            return expression;
        }
        case TokenKind::KwTrue:
        case TokenKind::KwFalse: {
            auto expression = std::make_unique<Bool>();
            expression->value = token.kind == TokenKind::KwTrue;
            expression->span = spanOfToken(token);
            advance();
            return expression;
        }
        case TokenKind::Identifier: {
            auto expression = std::make_unique<Variable>();
            expression->name = Name{token.text, spanOfToken(token)};
            expression->span = expression->name.span;
            advance();
            return expression;
        }
        case TokenKind::LeftParen:
            return parseGroupingOrPoint();
        case TokenKind::LeftBracket:
            return parseList();
        case TokenKind::KwRef:
            setDiag(ErrorCode::S004, spanOfToken(token), "ref 使用位置非法");
            return nullptr;
        default:
            failExpressionExpected(operandExpected);
            return nullptr;
        }
    }

    void popExpressionStart() {
        if (!expressionStarts_.empty()) {
            expressionStarts_.pop_back();
        }
    }

    ExprPtr parsePostfixOps(ExprPtr expression, bool allowIncDec) {
        while (!atEnd()) {
            if (current().wsBefore) {
                break;
            }

            if (check(TokenKind::LeftBracket)) {
                const Span openSpan = spanOfToken(current());
                advance();
                pushDelimiter(DelimiterKind::Bracket, openSpan);
                expressionStarts_.push_back(openSpan);

                ExprPtr index = parseExpression(false);
                if (!index) {
                    return nullptr;
                }
                if (!check(TokenKind::RightBracket)) {
                    if (atEnd()) {
                        return nullptr;
                    }
                    if (isCloser(current().kind) && !closerMatches(current().kind)) {
                        setDiag(ErrorCode::P005, spanOfToken(current()),
                                "括号不匹配");
                    } else {
                        failUnexpected();
                    }
                    return nullptr;
                }
                const Span closeSpan = spanOfToken(current());
                advance();
                popDelimiter();
                popExpressionStart();

                auto indexed = std::make_unique<Index>();
                indexed->receiver = std::move(expression);
                indexed->index = std::move(index);
                indexed->openSpan = openSpan;
                indexed->closeSpan = closeSpan;
                indexed->span = Span{indexed->receiver->span.start, closeSpan.end};
                expression = std::move(indexed);
                continue;
            }

            if (check(TokenKind::Dot)) {
                const Span dotSpan = spanOfToken(current());
                advance();
                if (!check(TokenKind::Identifier) || current().wsBefore) {
                    if (atEnd() || (isTerminator(current().kind) &&
                                    atStatementLevel())) {
                        incompleteMemberDot_ = dotSpan;
                        return nullptr;
                    }
                    setDiag(ErrorCode::P004, dotSpan,
                            "命令参数不是合法的表达式");
                    return nullptr;
                }
                const Token& memberToken = current();
                advance();

                auto member = std::make_unique<Member>();
                member->receiver = std::move(expression);
                member->member = Name{memberToken.text, spanOfToken(memberToken)};
                member->span = Span{member->receiver->span.start,
                                    member->member.span.end};
                expression = std::move(member);
                continue;
            }

            if (check(TokenKind::LeftParen)) {
                const Span lparenSpan = spanOfToken(current());
                const Span calleeStart = expression->span;
                advance();
                pushDelimiter(DelimiterKind::Paren, lparenSpan);
                expressionStarts_.push_back(Span{calleeStart.start, calleeStart.end});

                std::vector<ExprPtr> args;
                if (!check(TokenKind::RightParen)) {
                    while (true) {
                        ExprPtr arg = parseExpression(false);
                        if (!arg) {
                            return nullptr;
                        }
                        args.push_back(std::move(arg));

                        if (!check(TokenKind::Comma)) {
                            break;
                        }
                        advance();
                        if (check(TokenKind::RightParen)) {
                            setDiag(ErrorCode::P001, spanOfToken(current()),
                                    "意外的 token");
                            return nullptr;
                        }
                    }
                }

                if (!check(TokenKind::RightParen)) {
                    if (atEnd()) {
                        return nullptr;
                    }
                    if (isCloser(current().kind) && !closerMatches(current().kind)) {
                        setDiag(ErrorCode::P005, spanOfToken(current()),
                                "括号不匹配");
                    } else {
                        failUnexpected();
                    }
                    return nullptr;
                }

                const Span rparenSpan = spanOfToken(current());
                advance();
                popDelimiter();
                popExpressionStart();

                auto call = std::make_unique<Call>();
                call->callee = std::move(expression);
                call->args = std::move(args);
                call->lparenSpan = lparenSpan;
                call->rparenSpan = rparenSpan;
                call->span = Span{call->callee->span.start, rparenSpan.end};
                expression = std::move(call);
                continue;
            }

            if (allowIncDec &&
                (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus))) {
                const Span opSpan = spanOfToken(current());
                const IncDecOp op = check(TokenKind::PlusPlus)
                                        ? IncDecOp::Increment
                                        : IncDecOp::Decrement;
                advance();
                if (!isAssignable(*expression)) {
                    setDiag(ErrorCode::S009, opSpan, "自增/自减目标不可赋值");
                    return nullptr;
                }
                auto incDec = std::make_unique<IncDec>();
                incDec->op = op;
                incDec->prefix = false;
                incDec->opSpan = opSpan;
                incDec->target = std::move(expression);
                incDec->span = Span{incDec->target->span.start, opSpan.end};
                expression = std::move(incDec);
                continue;
            }

            break;
        }

        return expression;
    }

    ExprPtr parsePostfix(bool operandExpected) {
        ExprPtr expression = parsePrimary(operandExpected);
        if (!expression) {
            return nullptr;
        }
        return parsePostfixOps(std::move(expression), true);
    }

    ExprPtr parseUnary(bool operandExpected) {
        if (atEnd()) {
            return nullptr;
        }

        if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
            const Span opSpan = spanOfToken(current());
            const UnaryOp op = check(TokenKind::Minus) ? UnaryOp::Negate : UnaryOp::Not;
            advance();
            ExprPtr operand = parseUnary(true);
            if (!operand) {
                return nullptr;
            }
            auto unary = std::make_unique<Unary>();
            unary->op = op;
            unary->opSpan = opSpan;
            unary->operand = std::move(operand);
            unary->span = Span{opSpan.start, unary->operand->span.end};
            return unary;
        }

        if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
            const Span opSpan = spanOfToken(current());
            const IncDecOp op = check(TokenKind::PlusPlus) ? IncDecOp::Increment
                                                            : IncDecOp::Decrement;
            advance();
            ExprPtr target = parseUnary(true);
            if (!target) {
                return nullptr;
            }
            if (!isAssignable(*target)) {
                setDiag(ErrorCode::S009, opSpan, "自增/自减目标不可赋值");
                return nullptr;
            }
            auto incDec = std::make_unique<IncDec>();
            incDec->op = op;
            incDec->prefix = true;
            incDec->opSpan = opSpan;
            incDec->target = std::move(target);
            incDec->span = Span{opSpan.start, incDec->target->span.end};
            return incDec;
        }

        if (check(TokenKind::Star)) {
            const Span starSpan = spanOfToken(current());
            advance();
            if (atEnd()) {
                return nullptr;
            }

            const Token& token = current();
            if (token.kind == TokenKind::KwRef) {
                setDiag(ErrorCode::S004, spanOfToken(token), "ref 使用位置非法");
                return nullptr;
            }
            if (token.kind != TokenKind::Identifier || token.wsBefore) {
                if (isTerminator(token.kind) && atStatementLevel()) {
                    setDiag(ErrorCode::P002, Span{token.offset, token.offset},
                            "意外的结束", "缺少变量名");
                } else {
                    failUnexpected();
                }
                return nullptr;
            }

            auto variable = std::make_unique<Variable>();
            variable->name = Name{token.text, spanOfToken(token)};
            variable->span = variable->name.span;
            advance();

            auto deref = std::make_unique<Deref>();
            deref->starSpan = starSpan;
            deref->span = Span{starSpan.start, variable->span.end};
            deref->variable = std::move(variable);
            return deref;
        }

        return parsePostfix(operandExpected);
    }

    ExprPtr parseMul(bool operandExpected) {
        ExprPtr left = parseUnary(operandExpected);
        if (!left) {
            return nullptr;
        }
        while (check(TokenKind::Star) || check(TokenKind::Slash) ||
               check(TokenKind::Percent)) {
            const Token& opToken = current();
            const BinaryOp op = opToken.kind == TokenKind::Star
                                    ? BinaryOp::Multiply
                                    : (opToken.kind == TokenKind::Slash
                                           ? BinaryOp::Divide
                                           : BinaryOp::Modulo);
            const Span opSpan = spanOfToken(opToken);
            advance();
            ExprPtr right = parseUnary(true);
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Binary>();
            binary->op = op;
            binary->opSpan = opSpan;
            binary->left = std::move(left);
            binary->right = std::move(right);
            binary->span = Span{binary->left->span.start, binary->right->span.end};
            left = std::move(binary);
        }
        return left;
    }

    ExprPtr parseAdd(bool operandExpected) {
        ExprPtr left = parseMul(operandExpected);
        if (!left) {
            return nullptr;
        }
        while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
            const Token& opToken = current();
            const BinaryOp op =
                opToken.kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Subtract;
            const Span opSpan = spanOfToken(opToken);
            advance();
            ExprPtr right = parseMul(true);
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Binary>();
            binary->op = op;
            binary->opSpan = opSpan;
            binary->left = std::move(left);
            binary->right = std::move(right);
            binary->span = Span{binary->left->span.start, binary->right->span.end};
            left = std::move(binary);
        }
        return left;
    }

    ExprPtr parseRelational(bool operandExpected) {
        ExprPtr left = parseAdd(operandExpected);
        if (!left) {
            return nullptr;
        }
        while (check(TokenKind::Less) || check(TokenKind::LessEqual) ||
               check(TokenKind::Greater) || check(TokenKind::GreaterEqual)) {
            const Token& opToken = current();
            BinaryOp op = BinaryOp::Less;
            switch (opToken.kind) {
            case TokenKind::Less: op = BinaryOp::Less; break;
            case TokenKind::LessEqual: op = BinaryOp::LessEqual; break;
            case TokenKind::Greater: op = BinaryOp::Greater; break;
            case TokenKind::GreaterEqual: op = BinaryOp::GreaterEqual; break;
            default: break;
            }
            const Span opSpan = spanOfToken(opToken);
            advance();
            ExprPtr right = parseAdd(true);
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Binary>();
            binary->op = op;
            binary->opSpan = opSpan;
            binary->left = std::move(left);
            binary->right = std::move(right);
            binary->span = Span{binary->left->span.start, binary->right->span.end};
            left = std::move(binary);
        }
        return left;
    }

    ExprPtr parseEquality(bool operandExpected) {
        ExprPtr left = parseRelational(operandExpected);
        if (!left) {
            return nullptr;
        }
        while (check(TokenKind::Equal) || check(TokenKind::NotEqual)) {
            const Token& opToken = current();
            const BinaryOp op =
                opToken.kind == TokenKind::Equal ? BinaryOp::Equal : BinaryOp::NotEqual;
            const Span opSpan = spanOfToken(opToken);
            advance();
            ExprPtr right = parseRelational(true);
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Binary>();
            binary->op = op;
            binary->opSpan = opSpan;
            binary->left = std::move(left);
            binary->right = std::move(right);
            binary->span = Span{binary->left->span.start, binary->right->span.end};
            left = std::move(binary);
        }
        return left;
    }

    ExprPtr parseAnd(bool operandExpected) {
        ExprPtr left = parseEquality(operandExpected);
        if (!left) {
            return nullptr;
        }
        while (check(TokenKind::AmpAmp)) {
            const Span opSpan = spanOfToken(current());
            advance();
            ExprPtr right = parseEquality(true);
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Binary>();
            binary->op = BinaryOp::LogicalAnd;
            binary->opSpan = opSpan;
            binary->left = std::move(left);
            binary->right = std::move(right);
            binary->span = Span{binary->left->span.start, binary->right->span.end};
            left = std::move(binary);
        }
        return left;
    }

    ExprPtr parseOr(bool operandExpected) {
        ExprPtr left = parseAnd(operandExpected);
        if (!left) {
            return nullptr;
        }
        while (check(TokenKind::PipePipe)) {
            const Span opSpan = spanOfToken(current());
            advance();
            ExprPtr right = parseAnd(true);
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Binary>();
            binary->op = BinaryOp::LogicalOr;
            binary->opSpan = opSpan;
            binary->left = std::move(left);
            binary->right = std::move(right);
            binary->span = Span{binary->left->span.start, binary->right->span.end};
            left = std::move(binary);
        }
        return left;
    }

    ExprPtr parseTernary(bool operandExpected) {
        ExprPtr condition = parseOr(operandExpected);
        if (!condition) {
            return nullptr;
        }
        if (!check(TokenKind::Question)) {
            return condition;
        }

        const Span questionSpan = spanOfToken(current());
        advance();
        ExprPtr thenExpr = parseExpression(true);
        if (!thenExpr) {
            return nullptr;
        }

        if (!check(TokenKind::Colon)) {
            if (atEnd()) {
                return nullptr;
            }
            if (isTerminator(current().kind) && atStatementLevel()) {
                setDiag(ErrorCode::P002, Span{current().offset, current().offset},
                        "意外的结束", "缺少 :");
            } else {
                failUnexpected();
            }
            return nullptr;
        }

        const Span colonSpan = spanOfToken(current());
        advance();
        ExprPtr elseExpr = parseExpression(true);
        if (!elseExpr) {
            return nullptr;
        }

        auto ternary = std::make_unique<Ternary>();
        ternary->cond = std::move(condition);
        ternary->thenExpr = std::move(thenExpr);
        ternary->elseExpr = std::move(elseExpr);
        ternary->questionSpan = questionSpan;
        ternary->colonSpan = colonSpan;
        ternary->span = Span{ternary->cond->span.start, ternary->elseExpr->span.end};
        return ternary;
    }

public:
    struct SplitResult {
        std::vector<CommandWord> words;
        std::size_t stop = 0;
    };

    static SplitResult splitWords(const std::vector<Token>& tokens, std::size_t begin,
                                  std::size_t end) {
        SplitResult result;
        end = std::min(end, tokens.size());

        std::size_t depth = 0;
        std::size_t wordStart = LexResult::npos;

        auto wordEnd = [&tokens](std::size_t stopIndex) {
            if (stopIndex >= tokens.size() ||
                tokens[stopIndex].kind == TokenKind::EndOfFile) {
                return WordEnd::InputEnd;
            }
            if (isTerminator(tokens[stopIndex].kind)) {
                return WordEnd::StatementTerminator;
            }
            return WordEnd::WordBoundary;
        };

        std::size_t index = begin;
        for (; index < end; ++index) {
            const Token& token = tokens[index];

            if (depth == 0 && isTerminator(token.kind)) {
                break;
            }

            if (depth == 0 && wordStart != LexResult::npos && token.wsBefore) {
                result.words.push_back(
                    CommandWord{TokenRange{wordStart, index}, WordEnd::WordBoundary});
                wordStart = LexResult::npos;
            }

            if (wordStart == LexResult::npos) {
                wordStart = index;
            }

            if (token.kind == TokenKind::LeftParen ||
                token.kind == TokenKind::LeftBracket) {
                ++depth;
            } else if (token.kind == TokenKind::RightParen ||
                       token.kind == TokenKind::RightBracket) {
                if (depth > 0) {
                    --depth;
                }
            }
        }

        if (wordStart != LexResult::npos) {
            result.words.push_back(
                CommandWord{TokenRange{wordStart, index}, wordEnd(index)});
        }

        result.stop = index;
        return result;
    }

private:
    const LexResult& lex_;
    const std::vector<Token>& tokens_;
    ParseOptions options_;
    bool inWord_ = false;
    std::size_t limit_ = 0;
    std::size_t pos_ = 0;
    std::size_t endOffset_ = 0;
    Span wordStartSpan_{};
    Token syntheticEof_{};
    std::optional<Diagnostic> diagnostic_;
    bool needStatement_ = false;
    bool deferrableRefMissingRhs_ = false;
    bool parseStopped_ = false;
    std::optional<IncompleteReason> subIncomplete_;
    std::optional<Span> incompleteMemberDot_;
    std::vector<Delimiter> delimiters_;
    std::vector<Span> expressionStarts_;
};

std::optional<IncompleteReason> unclosedOrSub(
    const Parser& parser, const std::optional<IncompleteReason>& sub) {
    if (sub &&
        (*sub == IncompleteReason::UnclosedBrace ||
         *sub == IncompleteReason::UnclosedParen ||
         *sub == IncompleteReason::UnclosedBracket)) {
        return sub;
    }
    if (parser.hasUnclosed()) {
        return parser.innermostUnclosed();
    }
    return std::nullopt;
}

bool isDeferrableP002(const LexResult& lex, const Diagnostic& diagnostic) {
    return diagnostic.code == ErrorCode::P002 && lex.endsWithContinuation &&
           lex.lastContinuationOffset != LexResult::npos &&
           diagnostic.span.start >= lex.lastContinuationOffset;
}

bool isDeferrableS004(const LexResult& lex, const Parser& parser,
                      const Diagnostic& diagnostic) {
    return diagnostic.code == ErrorCode::S004 && lex.endsWithContinuation &&
           parser.deferrableRefMissingRhs();
}

ExprParseResult parseWordRange(const LexResult& lex, TokenRange range, WordEnd end,
                               ParseOptions options, bool handleLexDiagnostic) {
    ExprParseResult result;

    if (handleLexDiagnostic && lex.diagnostic) {
        if (lex.diagnostic->code != ErrorCode::L005 ||
            options == ParseOptions::Script) {
            result.status = ParseStatus::Error;
            result.diagnostic = lex.diagnostic;
            return result;
        }
    }

    if (range.begin >= range.end || range.begin >= lex.tokens.size()) {
        if (options == ParseOptions::Repl && end == WordEnd::InputEnd &&
            lex.endsWithContinuation) {
            result.status = ParseStatus::Incomplete;
            result.incompleteReason = IncompleteReason::TrailingContinuation;
        } else {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P002,
                                           Span{lex.endOffset, lex.endOffset},
                                           "意外的结束", "缺少表达式"};
        }
        return result;
    }

    Parser parser(lex, range.begin, range.end, options, true);
    ExprPtr expression = parser.parseWordExpression();

    if (parser.diagnostic()) {
        const Diagnostic& diagnostic = *parser.diagnostic();
        if (diagnostic.code == ErrorCode::P001 &&
            parser.position() == range.begin) {
            const TokenKind kind = lex.tokens[range.begin].kind;
            if (kind != TokenKind::RightParen && kind != TokenKind::RightBracket) {
                result.status = ParseStatus::Error;
                result.diagnostic =
                    Diagnostic{ErrorCode::P004,
                               spanOfToken(lex.tokens[range.begin]),
                               "命令参数不是合法的表达式", {}};
                return result;
            }
        }

        if (options == ParseOptions::Repl && end == WordEnd::InputEnd &&
            isDeferrableP002(lex, diagnostic)) {
            result.status = ParseStatus::Incomplete;
            result.incompleteReason = IncompleteReason::TrailingContinuation;
            return result;
        }

        result.status = ParseStatus::Error;
        result.diagnostic = diagnostic;
        return result;
    }

    if (!expression) {
        if (auto unclosed = unclosedOrSub(parser, parser.subIncomplete())) {
            if (options == ParseOptions::Script) {
                result.status = ParseStatus::Error;
                result.diagnostic = Diagnostic{ErrorCode::P002,
                                               Span{lex.endOffset, lex.endOffset},
                                               "意外的结束", "缺少闭括号"};
            } else {
                result.status = ParseStatus::Incomplete;
                result.incompleteReason = unclosed;
            }
            return result;
        }

        if (auto dot = parser.incompleteMemberDot();
            dot && end == WordEnd::WordBoundary) {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P004, *dot,
                                           "命令参数不是合法的表达式", {}};
            return result;
        }

        switch (end) {
        case WordEnd::WordBoundary:
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P004,
                                           spanOfToken(lex.tokens[range.begin]),
                                           "命令参数不是合法的表达式", {}};
            return result;
        case WordEnd::StatementTerminator: {
            const Token& token = range.end < lex.tokens.size()
                                     ? lex.tokens[range.end]
                                     : lex.tokens.back();
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P002,
                                           Span{token.offset, token.offset},
                                           "意外的结束", "缺少表达式"};
            return result;
        }
        case WordEnd::InputEnd:
            if (options == ParseOptions::Repl && lex.endsWithContinuation) {
                result.status = ParseStatus::Incomplete;
                result.incompleteReason = IncompleteReason::TrailingContinuation;
            } else {
                result.status = ParseStatus::Error;
                result.diagnostic = Diagnostic{ErrorCode::P002,
                                               Span{lex.endOffset, lex.endOffset},
                                               "意外的结束", "缺少表达式"};
            }
            return result;
        }
    }

    if (parser.position() != range.end) {
        const Token& token = lex.tokens[parser.position()];
        if (token.kind == TokenKind::RightParen ||
            token.kind == TokenKind::RightBracket) {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P001, spanOfToken(token),
                                           "意外的 token", {}};
            return result;
        }
        if (dynamic_cast<const Deref*>(expression.get()) != nullptr &&
            (token.kind == TokenKind::LeftBracket || token.kind == TokenKind::Dot ||
             token.kind == TokenKind::LeftParen ||
             token.kind == TokenKind::PlusPlus ||
             token.kind == TokenKind::MinusMinus)) {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P001, spanOfToken(token),
                                           "意外的 token", {}};
            return result;
        }

        result.status = ParseStatus::Error;
        result.diagnostic = Diagnostic{ErrorCode::P004,
                                       spanOfToken(lex.tokens[range.begin]),
                                       "命令参数不是合法的表达式", {}};
        return result;
    }

    normalizeBoundarySpans(*expression);

    if (auto boundary = validateBoundary(*expression)) {
        result.status = ParseStatus::Error;
        result.diagnostic = boundary;
        return result;
    }

    if (end == WordEnd::InputEnd && options == ParseOptions::Repl &&
        lex.endsWithContinuation) {
        result.status = ParseStatus::Incomplete;
        result.incompleteReason = IncompleteReason::TrailingContinuation;
        return result;
    }

    result.status = ParseStatus::Ok;
    result.expression = std::move(expression);
    return result;
}

ExprParseResult parseExpressionRange(const LexResult& lex, TokenRange range,
                                     Boundary boundary, ParseOptions options,
                                     bool handleLexDiagnostic) {
    ExprParseResult result;

    if (handleLexDiagnostic && lex.diagnostic) {
        if (lex.diagnostic->code != ErrorCode::L005 ||
            options == ParseOptions::Script) {
            result.status = ParseStatus::Error;
            result.diagnostic = lex.diagnostic;
            return result;
        }
    }

    if (range.begin >= range.end || range.begin >= lex.tokens.size()) {
        if (options == ParseOptions::Repl && boundary == Boundary::InputEnd &&
            lex.endsWithContinuation) {
            result.status = ParseStatus::Incomplete;
            result.incompleteReason = IncompleteReason::TrailingContinuation;
        } else {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P002,
                                           Span{lex.endOffset, lex.endOffset},
                                           "意外的结束", "缺少表达式"};
        }
        return result;
    }

    Parser parser(lex, range.begin, range.end, options, false);
    ExprPtr expression = parser.parseExpressionBody();

    if (parser.diagnostic()) {
        const Diagnostic& diagnostic = *parser.diagnostic();
        if (options == ParseOptions::Repl && boundary == Boundary::InputEnd &&
            isDeferrableP002(lex, diagnostic)) {
            result.status = ParseStatus::Incomplete;
            result.incompleteReason = IncompleteReason::TrailingContinuation;
            return result;
        }
        result.status = ParseStatus::Error;
        result.diagnostic = diagnostic;
        return result;
    }

    if (!expression) {
        if (auto unclosed = unclosedOrSub(parser, parser.subIncomplete())) {
            if (options == ParseOptions::Script) {
                result.status = ParseStatus::Error;
                result.diagnostic = Diagnostic{ErrorCode::P002,
                                               Span{lex.endOffset, lex.endOffset},
                                               "意外的结束", "缺少闭括号"};
            } else {
                result.status = ParseStatus::Incomplete;
                result.incompleteReason = unclosed;
            }
            return result;
        }

        if (auto dot = parser.incompleteMemberDot();
            dot && boundary == Boundary::WordBoundary) {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P004, *dot,
                                           "命令参数不是合法的表达式", {}};
            return result;
        }

        switch (boundary) {
        case Boundary::WordBoundary:
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P004,
                                           spanOfToken(lex.tokens[range.begin]),
                                           "命令参数不是合法的表达式", {}};
            return result;
        case Boundary::StatementTerminator: {
            const Token& token = range.end < lex.tokens.size()
                                     ? lex.tokens[range.end]
                                     : lex.tokens.back();
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P002,
                                           Span{token.offset, token.offset},
                                           "意外的结束", "缺少表达式"};
            return result;
        }
        case Boundary::InputEnd:
            if (options == ParseOptions::Repl && lex.endsWithContinuation) {
                result.status = ParseStatus::Incomplete;
                result.incompleteReason = IncompleteReason::TrailingContinuation;
            } else {
                result.status = ParseStatus::Error;
                result.diagnostic = Diagnostic{ErrorCode::P002,
                                               Span{lex.endOffset, lex.endOffset},
                                               "意外的结束", "缺少表达式"};
            }
            return result;
        case Boundary::ClosingParen:
        case Boundary::ClosingBracket:
        case Boundary::Comma:
        case Boundary::Colon: {
            const Span boundarySpan =
                range.end < lex.tokens.size()
                    ? spanOfToken(lex.tokens[range.end])
                    : Span{lex.endOffset, lex.endOffset};
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P001, boundarySpan,
                                           "意外的 token", {}};
            return result;
        }
        }
    }

    if (parser.position() != range.end) {
        const Token& token = lex.tokens[parser.position()];
        result.status = ParseStatus::Error;
        result.diagnostic = Diagnostic{ErrorCode::P001, spanOfToken(token),
                                       "意外的 token", {}};
        return result;
    }

    normalizeBoundarySpans(*expression);

    if (auto boundaryDiag = validateBoundary(*expression)) {
        result.status = ParseStatus::Error;
        result.diagnostic = boundaryDiag;
        return result;
    }

    if (boundary == Boundary::InputEnd && options == ParseOptions::Repl &&
        lex.endsWithContinuation) {
        result.status = ParseStatus::Incomplete;
        result.incompleteReason = IncompleteReason::TrailingContinuation;
        return result;
    }

    result.status = ParseStatus::Ok;
    result.expression = std::move(expression);
    return result;
}

} // namespace

ParseResult parse(const LexResult& lex, ParseOptions options) {
    ParseResult result;

    if (lex.diagnostic) {
        if (lex.diagnostic->code != ErrorCode::L005 ||
            options == ParseOptions::Script) {
            result.status = ParseStatus::Error;
            result.diagnostic = lex.diagnostic;
            return result;
        }
    }

    std::size_t limit = lex.tokens.size();
    if (!lex.tokens.empty() && lex.tokens.back().kind == TokenKind::EndOfFile) {
        limit = lex.tokens.size() - 1;
    }

    Parser parser(lex, 0, limit, options, false);
    std::unique_ptr<Program> program = parser.parseProgram();

    if (parser.diagnostic()) {
        const Diagnostic& diagnostic = *parser.diagnostic();
        const bool p002AtEof =
            diagnostic.code == ErrorCode::P002 &&
            diagnostic.span.start >= lex.endOffset;
        const bool p002Trailing = isDeferrableP002(lex, diagnostic);
        const bool s004Trailing = isDeferrableS004(lex, parser, diagnostic);

        if (options == ParseOptions::Repl &&
            (p002AtEof || p002Trailing || s004Trailing)) {
            if (auto unclosed = unclosedOrSub(parser, parser.subIncomplete())) {
                result.status = ParseStatus::Incomplete;
                result.incompleteReason = unclosed;
                return result;
            }
            if (parser.needStatement()) {
                result.status = ParseStatus::Incomplete;
                result.incompleteReason = IncompleteReason::NeedStatement;
                return result;
            }
            if (p002Trailing || s004Trailing) {
                result.status = ParseStatus::Incomplete;
                result.incompleteReason = IncompleteReason::TrailingContinuation;
                return result;
            }
        }

        result.status = ParseStatus::Error;
        result.diagnostic = diagnostic;
        return result;
    }

    if (auto unclosed = unclosedOrSub(parser, parser.subIncomplete())) {
        if (options == ParseOptions::Repl) {
            result.status = ParseStatus::Incomplete;
            result.incompleteReason = unclosed;
        } else {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P002,
                                           Span{lex.endOffset, lex.endOffset},
                                           "意外的结束", "缺少闭括号"};
        }
        return result;
    }

    if (parser.needStatement()) {
        if (options == ParseOptions::Repl) {
            result.status = ParseStatus::Incomplete;
            result.incompleteReason = IncompleteReason::NeedStatement;
        } else {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P002,
                                           Span{lex.endOffset, lex.endOffset},
                                           "意外的结束", "缺少语句"};
        }
        return result;
    }

    if (parser.parseStopped()) {
        const std::size_t offset = parser.failureOffset();
        if (options == ParseOptions::Repl && lex.endsWithContinuation &&
            lex.lastContinuationOffset != LexResult::npos &&
            offset >= lex.lastContinuationOffset) {
            result.status = ParseStatus::Incomplete;
            result.incompleteReason = IncompleteReason::TrailingContinuation;
        } else {
            result.status = ParseStatus::Error;
            result.diagnostic = Diagnostic{ErrorCode::P002, Span{offset, offset},
                                           "意外的结束", "缺少表达式"};
        }
        return result;
    }

    if (options == ParseOptions::Repl && lex.endsWithContinuation) {
        result.status = ParseStatus::Incomplete;
        result.incompleteReason = IncompleteReason::TrailingContinuation;
        return result;
    }

    if (!program) {
        result.status = ParseStatus::Error;
        result.diagnostic = Diagnostic{ErrorCode::P002,
                                       Span{lex.endOffset, lex.endOffset},
                                       "意外的结束", "缺少语句"};
        return result;
    }

    normalizeBoundarySpans(*program);

    if (auto boundary = validateBoundary(*program)) {
        result.status = ParseStatus::Error;
        result.diagnostic = boundary;
        return result;
    }

    result.status = ParseStatus::Ok;
    result.program = std::move(program);
    return result;
}

ExprParseResult parseExpression(const LexResult& lex, TokenRange range,
                                Boundary boundary, ParseOptions options) {
    return parseExpressionRange(lex, range, boundary, options, true);
}

ExprParseResult parseWord(const LexResult& lex, TokenRange range, WordEnd end,
                          ParseOptions options) {
    return parseWordRange(lex, range, end, options, true);
}

std::vector<CommandWord> splitCommandWords(const std::vector<Token>& tokens,
                                           std::size_t begin, std::size_t end) {
    Parser::SplitResult result = Parser::splitWords(tokens, begin, end);
    return std::move(result.words);
}

} // namespace lush::core

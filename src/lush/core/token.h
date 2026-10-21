#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lush::core {

enum class TokenKind {
    EndOfFile,
    Newline,
    Identifier,
    Int,
    BoundaryInt,
    Float,
    String,

    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwIn,
    KwTrue,
    KwFalse,
    KwRef,
    KwBreak,
    KwContinue,
    KwDef,
    KwReturn,

    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Colon,
    Question,
    Dot,

    Assign,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    AmpAmp,
    PipePipe,
    PlusPlus,
    MinusMinus,
};

constexpr std::string_view tokenKindName(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::EndOfFile: return "EOF";
    case TokenKind::Newline: return "NL";
    case TokenKind::Identifier: return "IDENT";
    case TokenKind::Int: return "INT";
    case TokenKind::BoundaryInt: return "BOUNDARY_INT";
    case TokenKind::Float: return "FLOAT";
    case TokenKind::String: return "STRING";
    case TokenKind::KwIf: return "IF";
    case TokenKind::KwElse: return "ELSE";
    case TokenKind::KwWhile: return "WHILE";
    case TokenKind::KwFor: return "FOR";
    case TokenKind::KwIn: return "IN";
    case TokenKind::KwTrue: return "TRUE";
    case TokenKind::KwFalse: return "FALSE";
    case TokenKind::KwRef: return "REF";
    case TokenKind::KwBreak: return "BREAK";
    case TokenKind::KwContinue: return "CONTINUE";
    case TokenKind::KwDef: return "DEF";
    case TokenKind::KwReturn: return "RETURN";
    case TokenKind::LeftParen: return "LPAREN";
    case TokenKind::RightParen: return "RPAREN";
    case TokenKind::LeftBracket: return "LBRACKET";
    case TokenKind::RightBracket: return "RBRACKET";
    case TokenKind::LeftBrace: return "LBRACE";
    case TokenKind::RightBrace: return "RBRACE";
    case TokenKind::Comma: return "COMMA";
    case TokenKind::Semicolon: return "SEMICOLON";
    case TokenKind::Colon: return "COLON";
    case TokenKind::Question: return "QUESTION";
    case TokenKind::Dot: return "DOT";
    case TokenKind::Assign: return "ASSIGN";
    case TokenKind::Equal: return "EQ";
    case TokenKind::NotEqual: return "NE";
    case TokenKind::Less: return "LT";
    case TokenKind::LessEqual: return "LE";
    case TokenKind::Greater: return "GT";
    case TokenKind::GreaterEqual: return "GE";
    case TokenKind::Plus: return "PLUS";
    case TokenKind::Minus: return "MINUS";
    case TokenKind::Star: return "STAR";
    case TokenKind::Slash: return "SLASH";
    case TokenKind::Percent: return "PERCENT";
    case TokenKind::Bang: return "BANG";
    case TokenKind::AmpAmp: return "ANDAND";
    case TokenKind::PipePipe: return "OROR";
    case TokenKind::PlusPlus: return "PLUSPLUS";
    case TokenKind::MinusMinus: return "MINUSMINUS";
    }
    return "UNKNOWN";
}

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::size_t offset = 0;
    std::size_t length = 0;
    bool wsBefore = false;
    std::string text;
};

} // namespace lush::core

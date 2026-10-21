#include "test_harness.h"

#include "lush/lush.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using lush::core::ErrorCode;
using lush::core::LexResult;
using lush::core::TokenKind;

struct Lexed {
    lush::core::Source source;
    LexResult result;
};

Lexed lex(std::string text) {
    lush::core::Source source(std::move(text));
    lush::core::Lexer lexer(source);
    LexResult result = lexer.tokenize();
    return Lexed{std::move(source), std::move(result)};
}

bool kindsAre(const std::vector<lush::core::Token>& tokens,
              std::initializer_list<TokenKind> kinds) {
    if (tokens.size() != kinds.size()) {
        return false;
    }

    std::size_t index = 0;
    for (TokenKind kind : kinds) {
        if (tokens[index].kind != kind) {
            return false;
        }
        ++index;
    }
    return true;
}

void expectError(const std::string& text, ErrorCode code, std::size_t line,
                 std::size_t column) {
    Lexed lexed = lex(text);
    LUSH_CHECK(lexed.result.diagnostic.has_value());
    if (!lexed.result.diagnostic) {
        return;
    }

    LUSH_CHECK(lexed.result.diagnostic->code == code);
    const lush::core::LineCol position =
        lexed.source.lineCol(lexed.result.diagnostic->span.start);
    LUSH_CHECK_EQ(position.line, line);
    LUSH_CHECK_EQ(position.column, column);
}

std::string bytes(std::initializer_list<unsigned char> values) {
    std::string text;
    for (unsigned char value : values) {
        text.push_back(static_cast<char>(value));
    }
    return text;
}

} // namespace

LUSH_TEST(utf8_decodes_and_counts_code_points) {
    LUSH_CHECK(lush::core::isValidUtf8("abc"));
    LUSH_CHECK(lush::core::isValidUtf8("\xE4\xB8\xAD"));
    LUSH_CHECK_EQ(lush::core::utf8Length("a\xE4\xB8\xAD"), std::size_t{2});

    const auto at = lush::core::utf8At("a\xE4\xB8\xAD", 1);
    LUSH_CHECK(at.has_value());
    if (at) {
        LUSH_CHECK_EQ(*at, std::string("\xE4\xB8\xAD"));
    }
    LUSH_CHECK(!lush::core::utf8At("a", 1).has_value());

    std::size_t invalidOffset = 99;
    LUSH_CHECK(!lush::core::isValidUtf8(bytes({0x80}), &invalidOffset));
    LUSH_CHECK_EQ(invalidOffset, std::size_t{0});
    LUSH_CHECK(!lush::core::isValidUtf8(bytes({0xC0, 0xAF})));
    LUSH_CHECK(!lush::core::isValidUtf8(bytes({0xED, 0xA0, 0x80})));
    LUSH_CHECK(!lush::core::isValidUtf8(bytes({0xF4, 0x90, 0x80, 0x80})));

    std::string encoded;
    LUSH_CHECK(lush::core::appendUtf8(encoded, 0x1F600));
    LUSH_CHECK(lush::core::isValidUtf8(encoded));
    LUSH_CHECK_EQ(lush::core::utf8Length(encoded), std::size_t{1});
    LUSH_CHECK(!lush::core::appendUtf8(encoded, 0xD800));
    LUSH_CHECK(!lush::core::appendUtf8(encoded, 0x110000));
}

LUSH_TEST(lexer_empty_source_emits_eof) {
    Lexed lexed = lex("");
    LUSH_CHECK(kindsAre(lexed.result.tokens, {TokenKind::EndOfFile}));
    LUSH_CHECK(!lexed.result.diagnostic.has_value());
    LUSH_CHECK_EQ(lexed.source.lineCount(), std::size_t{1});
    LUSH_CHECK_EQ(lexed.source.lineText(1), std::string_view{});
    LUSH_CHECK_EQ(lexed.result.tokens[0].offset, std::size_t{0});
    LUSH_CHECK_EQ(lexed.result.tokens[0].length, std::size_t{0});
    LUSH_CHECK(!lexed.result.tokens[0].wsBefore);
    LUSH_CHECK(lexed.result.tokens[0].text.empty());
}

LUSH_TEST(lexer_skips_bom_but_keeps_offsets) {
    Lexed lexed = lex("\xEF\xBB\xBF" "x");
    LUSH_CHECK(lexed.source.hasBom());
    LUSH_CHECK_EQ(lexed.source.contentStart(), std::size_t{3});
    LUSH_CHECK(kindsAre(lexed.result.tokens,
                        {TokenKind::Identifier, TokenKind::EndOfFile}));

    const auto& identifier = lexed.result.tokens[0];
    LUSH_CHECK_EQ(identifier.offset, std::size_t{3});
    LUSH_CHECK_EQ(identifier.length, std::size_t{1});
    LUSH_CHECK_EQ(identifier.text, std::string("x"));
    LUSH_CHECK(!identifier.wsBefore);

    const auto position = lexed.source.lineCol(3);
    LUSH_CHECK_EQ(position.line, std::size_t{1});
    LUSH_CHECK_EQ(position.column, std::size_t{1});
    LUSH_CHECK_EQ(lexed.source.lineText(1), std::string_view("x"));
}

LUSH_TEST(lexer_splits_line_endings) {
    Lexed lexed = lex("a\nb\r\nc\rd");
    LUSH_CHECK(kindsAre(lexed.result.tokens,
                        {TokenKind::Identifier, TokenKind::Newline,
                         TokenKind::Identifier, TokenKind::Newline,
                         TokenKind::Identifier, TokenKind::Newline,
                         TokenKind::Identifier, TokenKind::EndOfFile}));

    LUSH_CHECK_EQ(lexed.result.tokens[1].offset, std::size_t{1});
    LUSH_CHECK_EQ(lexed.result.tokens[1].length, std::size_t{1});
    LUSH_CHECK_EQ(lexed.result.tokens[3].offset, std::size_t{3});
    LUSH_CHECK_EQ(lexed.result.tokens[3].length, std::size_t{2});
    LUSH_CHECK_EQ(lexed.result.tokens[5].offset, std::size_t{6});
    LUSH_CHECK_EQ(lexed.result.tokens[5].length, std::size_t{1});

    LUSH_CHECK_EQ(lexed.result.tokens[1].text, std::string("\n"));
    LUSH_CHECK_EQ(lexed.source.lineCount(), std::size_t{4});
    LUSH_CHECK_EQ(lexed.source.lineText(1), std::string_view("a"));
    LUSH_CHECK_EQ(lexed.source.lineText(2), std::string_view("b"));
    LUSH_CHECK_EQ(lexed.source.lineText(3), std::string_view("c"));
    LUSH_CHECK_EQ(lexed.source.lineText(4), std::string_view("d"));
}

LUSH_TEST(lexer_ignores_comments) {
    Lexed lexed = lex("a # comment\nb");
    LUSH_CHECK(kindsAre(lexed.result.tokens,
                        {TokenKind::Identifier, TokenKind::Newline,
                         TokenKind::Identifier, TokenKind::EndOfFile}));

    Lexed hashInString = lex("\"#\"");
    LUSH_CHECK(kindsAre(hashInString.result.tokens,
                        {TokenKind::String, TokenKind::EndOfFile}));
    LUSH_CHECK_EQ(hashInString.result.tokens[0].text, std::string("#"));
}

LUSH_TEST(lexer_removes_continuations) {
    Lexed lexed = lex("a = 1 + \\\n2");
    LUSH_CHECK(kindsAre(lexed.result.tokens,
                        {TokenKind::Identifier, TokenKind::Assign, TokenKind::Int,
                         TokenKind::Plus, TokenKind::Int, TokenKind::EndOfFile}));

    LUSH_CHECK_EQ(lexed.result.tokens[4].text, std::string("2"));
    const auto position = lexed.source.lineCol(lexed.result.tokens[4].offset);
    LUSH_CHECK_EQ(position.line, std::size_t{2});
    LUSH_CHECK_EQ(position.column, std::size_t{1});

    Lexed trailing = lex("a\\\n");
    LUSH_CHECK(kindsAre(trailing.result.tokens,
                        {TokenKind::Identifier, TokenKind::EndOfFile}));
    LUSH_CHECK_EQ(trailing.result.tokens[1].offset, std::size_t{3});
}

LUSH_TEST(lexer_concatenates_tokens_across_continuations) {
    Lexed identifier = lex("a\\\nb");
    LUSH_CHECK(kindsAre(identifier.result.tokens,
                        {TokenKind::Identifier, TokenKind::EndOfFile}));
    LUSH_CHECK_EQ(identifier.result.tokens[0].text, std::string("ab"));
    LUSH_CHECK_EQ(identifier.result.tokens[0].offset, std::size_t{0});
    LUSH_CHECK_EQ(identifier.result.tokens[0].length, std::size_t{4});
    LUSH_CHECK_EQ(identifier.result.tokens[0].text.size(), std::size_t{2});

    Lexed operatorJoin = lex("=\\\n=");
    LUSH_CHECK(kindsAre(operatorJoin.result.tokens,
                        {TokenKind::Equal, TokenKind::EndOfFile}));
    LUSH_CHECK_EQ(operatorJoin.result.tokens[0].text, std::string("=="));
    LUSH_CHECK_EQ(operatorJoin.result.tokens[0].length, std::size_t{4});

    Lexed separated = lex("a \\\nb");
    LUSH_CHECK(kindsAre(separated.result.tokens,
                        {TokenKind::Identifier, TokenKind::Identifier,
                         TokenKind::EndOfFile}));
    LUSH_CHECK_EQ(separated.result.tokens[1].text, std::string("b"));
    LUSH_CHECK_EQ(separated.result.tokens[1].offset, std::size_t{4});
    LUSH_CHECK(separated.result.tokens[1].wsBefore);
}

LUSH_TEST(lexer_tracks_ws_before) {
    Lexed spaced = lex("line 1, 2");
    LUSH_CHECK(kindsAre(spaced.result.tokens,
                        {TokenKind::Identifier, TokenKind::Int, TokenKind::Comma,
                         TokenKind::Int, TokenKind::EndOfFile}));
    LUSH_CHECK(!spaced.result.tokens[0].wsBefore);
    LUSH_CHECK(spaced.result.tokens[1].wsBefore);
    LUSH_CHECK(!spaced.result.tokens[2].wsBefore);
    LUSH_CHECK(spaced.result.tokens[3].wsBefore);

    Lexed tight = lex("line(1,2)");
    LUSH_CHECK(kindsAre(tight.result.tokens,
                        {TokenKind::Identifier, TokenKind::LeftParen, TokenKind::Int,
                         TokenKind::Comma, TokenKind::Int, TokenKind::RightParen,
                         TokenKind::EndOfFile}));
    LUSH_CHECK(!tight.result.tokens[1].wsBefore);
    LUSH_CHECK(!tight.result.tokens[2].wsBefore);

    Lexed negative = lex("line -1 -2");
    LUSH_CHECK(kindsAre(negative.result.tokens,
                        {TokenKind::Identifier, TokenKind::Minus, TokenKind::Int,
                         TokenKind::Minus, TokenKind::Int, TokenKind::EndOfFile}));
    LUSH_CHECK(negative.result.tokens[1].wsBefore);
    LUSH_CHECK(!negative.result.tokens[2].wsBefore);
    LUSH_CHECK(negative.result.tokens[3].wsBefore);
    LUSH_CHECK(!negative.result.tokens[4].wsBefore);

    Lexed tabbed = lex("a\tb");
    LUSH_CHECK(kindsAre(tabbed.result.tokens,
                        {TokenKind::Identifier, TokenKind::Identifier,
                         TokenKind::EndOfFile}));
    LUSH_CHECK(tabbed.result.tokens[1].wsBefore);

    Lexed indented = lex("a\n  b");
    LUSH_CHECK(kindsAre(indented.result.tokens,
                        {TokenKind::Identifier, TokenKind::Newline,
                         TokenKind::Identifier, TokenKind::EndOfFile}));
    LUSH_CHECK(indented.result.tokens[2].wsBefore);
    const auto indentedPosition =
        indented.source.lineCol(indented.result.tokens[2].offset);
    LUSH_CHECK_EQ(indentedPosition.line, std::size_t{2});
    LUSH_CHECK_EQ(indentedPosition.column, std::size_t{3});

    Lexed unindented = lex("a\nb");
    LUSH_CHECK(!unindented.result.tokens[2].wsBefore);

    Lexed commentIndented = lex("a # c\n  b");
    LUSH_CHECK(commentIndented.result.tokens[2].wsBefore);

    Lexed newline = lex("\n");
    LUSH_CHECK(!newline.result.tokens[0].wsBefore);
    LUSH_CHECK(!newline.result.tokens[1].wsBefore);
}

LUSH_TEST(lexer_suppresses_newlines_inside_parentheses_and_brackets) {
    Lexed paren = lex("(a\nb)");
    LUSH_CHECK(kindsAre(paren.result.tokens,
                        {TokenKind::LeftParen, TokenKind::Identifier,
                         TokenKind::Identifier, TokenKind::RightParen,
                         TokenKind::EndOfFile}));

    Lexed bracket = lex("[a\nb]");
    LUSH_CHECK(kindsAre(bracket.result.tokens,
                        {TokenKind::LeftBracket, TokenKind::Identifier,
                         TokenKind::Identifier, TokenKind::RightBracket,
                         TokenKind::EndOfFile}));

    Lexed brace = lex("{a\nb}");
    LUSH_CHECK(kindsAre(brace.result.tokens,
                        {TokenKind::LeftBrace, TokenKind::Identifier,
                         TokenKind::Newline, TokenKind::Identifier,
                         TokenKind::RightBrace, TokenKind::EndOfFile}));

    Lexed unmatched = lex(")\na");
    LUSH_CHECK(kindsAre(unmatched.result.tokens,
                        {TokenKind::RightParen, TokenKind::Newline,
                         TokenKind::Identifier, TokenKind::EndOfFile}));
}

LUSH_TEST(lexer_recognizes_keywords_case_insensitively) {
    Lexed lexed = lex("IF True ELSE def return in ref break continue while for false");
    LUSH_CHECK(kindsAre(lexed.result.tokens,
                        {TokenKind::KwIf, TokenKind::KwTrue, TokenKind::KwElse,
                         TokenKind::KwDef, TokenKind::KwReturn, TokenKind::KwIn,
                         TokenKind::KwRef, TokenKind::KwBreak, TokenKind::KwContinue,
                         TokenKind::KwWhile, TokenKind::KwFor, TokenKind::KwFalse,
                         TokenKind::EndOfFile}));
    LUSH_CHECK_EQ(lexed.result.tokens[0].text, std::string("IF"));
    LUSH_CHECK_EQ(lexed.result.tokens[1].text, std::string("True"));

    Lexed prefix = lex("ifx");
    LUSH_CHECK(kindsAre(prefix.result.tokens,
                        {TokenKind::Identifier, TokenKind::EndOfFile}));
}

LUSH_TEST(lexer_reads_numeric_literals) {
    Lexed lexed = lex(
        "0 42 1.5 1e3 1.0e-3 1e+4 1E-2 0e-400 1e-323 9223372036854775807 "
        "9223372036854775808");
    LUSH_CHECK(kindsAre(lexed.result.tokens,
                        {TokenKind::Int, TokenKind::Int, TokenKind::Float,
                         TokenKind::Float, TokenKind::Float, TokenKind::Float,
                         TokenKind::Float, TokenKind::Float, TokenKind::Float,
                         TokenKind::Int, TokenKind::BoundaryInt,
                         TokenKind::EndOfFile}));

    LUSH_CHECK_EQ(lexed.result.tokens[0].text, std::string("0"));
    LUSH_CHECK_EQ(lexed.result.tokens[2].text, std::string("1.5"));
    LUSH_CHECK_EQ(lexed.result.tokens[10].text, std::string("9223372036854775808"));

    Lexed delimiters = lex("1+2 1,2 1) 1[0]");
    LUSH_CHECK(kindsAre(delimiters.result.tokens,
                        {TokenKind::Int, TokenKind::Plus, TokenKind::Int,
                         TokenKind::Int, TokenKind::Comma, TokenKind::Int,
                         TokenKind::Int, TokenKind::RightParen,
                         TokenKind::Int, TokenKind::LeftBracket, TokenKind::Int,
                         TokenKind::RightBracket, TokenKind::EndOfFile}));
}

LUSH_TEST(lexer_rejects_malformed_numbers_with_l004) {
    expectError("1.", ErrorCode::L004, 1, 1);
    expectError(".5", ErrorCode::L004, 1, 1);
    expectError("1e", ErrorCode::L004, 1, 1);
    expectError("1e+", ErrorCode::L004, 1, 1);
    expectError("1.e3", ErrorCode::L004, 1, 1);
    expectError("123abc", ErrorCode::L004, 1, 1);
    expectError("1_000", ErrorCode::L004, 1, 1);
    expectError("0x10", ErrorCode::L004, 1, 1);
    expectError("1.foo", ErrorCode::L004, 1, 1);
    expectError("1..2", ErrorCode::L004, 1, 1);
    expectError("1e3e4", ErrorCode::L004, 1, 1);
    expectError("1e309", ErrorCode::L004, 1, 1);
    expectError("1e-400", ErrorCode::L004, 1, 1);
    expectError("9223372036854775809", ErrorCode::L004, 1, 1);
    expectError("18446744073709551616", ErrorCode::L004, 1, 1);
    expectError("echo 1e309", ErrorCode::L004, 1, 6);

    Lexed afterNumber = lex("1$");
    LUSH_CHECK(afterNumber.result.diagnostic.has_value());
    if (afterNumber.result.diagnostic) {
        LUSH_CHECK(afterNumber.result.diagnostic->code == ErrorCode::L001);
        const auto position = afterNumber.source.lineCol(
            afterNumber.result.diagnostic->span.start);
        LUSH_CHECK_EQ(position.column, std::size_t{2});
    }
}

LUSH_TEST(lexer_decodes_string_literals) {
    Lexed tab = lex("\"a\\tb\"");
    LUSH_CHECK(kindsAre(tab.result.tokens,
                        {TokenKind::String, TokenKind::EndOfFile}));
    LUSH_CHECK_EQ(tab.result.tokens[0].text, std::string("a\tb"));
    LUSH_CHECK_EQ(tab.result.tokens[0].length, std::size_t{6});
    LUSH_CHECK_EQ(tab.result.tokens[0].text.size(), std::size_t{3});

    Lexed newline = lex("\"a\\nb\"");
    LUSH_CHECK_EQ(newline.result.tokens[0].text, std::string("a\nb"));

    Lexed unicode = lex("\"\\u4E2D\"");
    LUSH_CHECK_EQ(unicode.result.tokens[0].text, std::string("\xE4\xB8\xAD"));

    Lexed rawUnicode = lex("\"\xE4\xB8\xAD\"");
    LUSH_CHECK_EQ(rawUnicode.result.tokens[0].text, std::string("\xE4\xB8\xAD"));

    Lexed backslash = lex("\"\\\\\"");
    LUSH_CHECK_EQ(backslash.result.tokens[0].text, std::string("\\"));

    Lexed nul = lex("\"\\0\"");
    LUSH_CHECK_EQ(nul.result.tokens[0].text.size(), std::size_t{1});
    LUSH_CHECK(nul.result.tokens[0].text[0] == '\0');

    Lexed unicodeNul = lex("\"\\u0000\"");
    LUSH_CHECK_EQ(unicodeNul.result.tokens[0].text.size(), std::size_t{1});
    LUSH_CHECK(unicodeNul.result.tokens[0].text[0] == '\0');

    Lexed extraHex = lex("\"\\u12345\"");
    LUSH_CHECK_EQ(extraHex.result.tokens[0].text, std::string("\xE1\x88\xB4" "5"));
}

LUSH_TEST(lexer_rejects_bad_strings) {
    expectError("\"abc", ErrorCode::L002, 1, 1);
    expectError("\"abc\n\"", ErrorCode::L002, 1, 1);
    expectError(std::string("\"abc") + "\\", ErrorCode::L002, 1, 1);

    expectError("\"\\q\"", ErrorCode::L003, 1, 2);
    expectError("\"abc\\\n\"", ErrorCode::L003, 1, 5);
    expectError("\"\\u12\"", ErrorCode::L003, 1, 2);
    expectError("\"\\uZZZZ\"", ErrorCode::L003, 1, 2);
    expectError("\"\\uD800\"", ErrorCode::L003, 1, 2);
    expectError("\"\\uD83D\\uDE00\"", ErrorCode::L003, 1, 2);
}

LUSH_TEST(lexer_rejects_invalid_utf8_and_ascii_illegal_characters) {
    expectError(bytes({0x80}), ErrorCode::L001, 1, 1);
    expectError(bytes({0xC0, 0xAF}), ErrorCode::L001, 1, 1);
    expectError(bytes({0xED, 0xA0, 0x80}), ErrorCode::L001, 1, 1);
    expectError(bytes({0xF4, 0x90, 0x80, 0x80}), ErrorCode::L001, 1, 1);
    expectError(bytes({0xE4, 0xB8, 0xAD}), ErrorCode::L001, 1, 1);
    expectError("$", ErrorCode::L001, 1, 1);
    expectError(std::string(1, '\0'), ErrorCode::L001, 1, 1);

    std::string afterEcho = "echo ";
    afterEcho.push_back(static_cast<char>(0x80));
    expectError(afterEcho, ErrorCode::L001, 1, 6);

    std::string inComment = "# ";
    inComment.push_back(static_cast<char>(0x80));
    inComment.push_back('\n');
    expectError(inComment, ErrorCode::L001, 1, 3);

    std::string inString = "\"";
    inString.push_back(static_cast<char>(0x80));
    inString.push_back('"');
    expectError(inString, ErrorCode::L001, 1, 2);

    expectError(std::string("x") + "\xEF\xBB\xBF", ErrorCode::L001, 1, 2);

    Lexed unicodeComment = lex("# \xE4\xB8\xAD\nx");
    LUSH_CHECK(!unicodeComment.result.diagnostic.has_value());
    LUSH_CHECK(kindsAre(unicodeComment.result.tokens,
                        {TokenKind::Newline, TokenKind::Identifier,
                         TokenKind::EndOfFile}));
}

LUSH_TEST(lexer_distinguishes_l005_and_l006) {
    expectError("\\", ErrorCode::L005, 1, 1);
    expectError("\\   ", ErrorCode::L005, 1, 1);
    expectError("\\\t", ErrorCode::L005, 1, 1);

    expectError("\\ # comment", ErrorCode::L006, 1, 1);
    expectError("\\   # comment", ErrorCode::L006, 1, 1);
    expectError("\\x", ErrorCode::L006, 1, 1);
    expectError("a \\ b", ErrorCode::L006, 1, 3);
    expectError("\\\\\n", ErrorCode::L006, 1, 1);
    expectError("\\   \\\n", ErrorCode::L006, 1, 1);
    expectError("a\\\n\\\\\n", ErrorCode::L006, 2, 1);

    Lexed newline = lex("\\\n");
    LUSH_CHECK(kindsAre(newline.result.tokens, {TokenKind::EndOfFile}));
    LUSH_CHECK(!newline.result.diagnostic.has_value());

    Lexed spacedNewline = lex("\\   \n");
    LUSH_CHECK(kindsAre(spacedNewline.result.tokens, {TokenKind::EndOfFile}));
    LUSH_CHECK(!spacedNewline.result.diagnostic.has_value());

    Lexed crlf = lex("\\\r\n");
    LUSH_CHECK(kindsAre(crlf.result.tokens, {TokenKind::EndOfFile}));
    LUSH_CHECK(!crlf.result.diagnostic.has_value());

    Lexed cr = lex("\\\r");
    LUSH_CHECK(kindsAre(cr.result.tokens, {TokenKind::EndOfFile}));
    LUSH_CHECK(!cr.result.diagnostic.has_value());

    Lexed tabNewline = lex("\\\t\n");
    LUSH_CHECK(kindsAre(tabNewline.result.tokens, {TokenKind::EndOfFile}));
    LUSH_CHECK(!tabNewline.result.diagnostic.has_value());
}

LUSH_TEST(source_maps_lines_and_columns) {
    lush::core::Source source("a\nb\r\nc");
    LUSH_CHECK_EQ(source.lineCount(), std::size_t{3});
    LUSH_CHECK_EQ(source.lineText(1), std::string_view("a"));
    LUSH_CHECK_EQ(source.lineText(2), std::string_view("b"));
    LUSH_CHECK_EQ(source.lineText(3), std::string_view("c"));

    auto position = source.lineCol(0);
    LUSH_CHECK_EQ(position.line, std::size_t{1});
    LUSH_CHECK_EQ(position.column, std::size_t{1});

    position = source.lineCol(2);
    LUSH_CHECK_EQ(position.line, std::size_t{2});
    LUSH_CHECK_EQ(position.column, std::size_t{1});

    position = source.lineCol(5);
    LUSH_CHECK_EQ(position.line, std::size_t{3});
    LUSH_CHECK_EQ(position.column, std::size_t{1});

    lush::core::Source trailing("a\n");
    LUSH_CHECK_EQ(trailing.lineCount(), std::size_t{2});
    LUSH_CHECK_EQ(trailing.lineText(2), std::string_view{});
    position = trailing.lineCol(2);
    LUSH_CHECK_EQ(position.line, std::size_t{2});
    LUSH_CHECK_EQ(position.column, std::size_t{1});

    lush::core::Source empty("");
    LUSH_CHECK_EQ(empty.lineCount(), std::size_t{1});
    LUSH_CHECK_EQ(empty.lineText(1), std::string_view{});

    lush::core::Source bom("\xEF\xBB\xBF" "a\r\nb");
    LUSH_CHECK_EQ(bom.lineCount(), std::size_t{2});
    LUSH_CHECK_EQ(bom.lineText(1), std::string_view("a"));
    LUSH_CHECK_EQ(bom.lineText(2), std::string_view("b"));
    position = bom.lineCol(3);
    LUSH_CHECK_EQ(position.line, std::size_t{1});
    LUSH_CHECK_EQ(position.column, std::size_t{1});
    position = bom.lineCol(6);
    LUSH_CHECK_EQ(position.line, std::size_t{2});
    LUSH_CHECK_EQ(position.column, std::size_t{1});
    LUSH_CHECK_EQ(bom.lineTextAt(6), std::string_view("b"));
    LUSH_CHECK_EQ(bom.slice(lush::core::Span{3, 4}), std::string_view("a"));

    LUSH_CHECK_EQ(source.slice(lush::core::Span{99, 100}), std::string_view{});
}

LUSH_TEST(diagnostic_format_matches_spec_shape) {
    Lexed lexed = lex("a \\ # comment");
    LUSH_CHECK(lexed.result.diagnostic.has_value());
    if (!lexed.result.diagnostic) {
        return;
    }

    const std::string formatted =
        lush::core::formatDiagnostic(lexed.source, *lexed.result.diagnostic);
    LUSH_CHECK(formatted.find("error[L006]") != std::string::npos);
    LUSH_CHECK(formatted.find("--> 1:3") != std::string::npos);
    LUSH_CHECK(formatted.find("^") != std::string::npos);
    LUSH_CHECK(formatted.find("= help:") != std::string::npos);
}

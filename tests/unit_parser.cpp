#include "test_harness.h"

#include "lush/core/ast.h"
#include "lush/core/lexer.h"
#include "lush/core/parser.h"
#include "lush/core/source.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

using lush::core::Boundary;
using lush::core::ErrorCode;
using lush::core::ExprParseResult;
using lush::core::IncompleteReason;
using lush::core::LexResult;
using lush::core::Lexer;
using lush::core::ParseOptions;
using lush::core::ParseResult;
using lush::core::ParseStatus;
using lush::core::Source;
using lush::core::TokenRange;
using lush::core::WordEnd;

LexResult lexText(const Source& source) {
    Lexer lexer(source);
    return lexer.tokenize();
}

std::size_t eofIndex(const LexResult& lex) {
    if (!lex.tokens.empty() &&
        lex.tokens.back().kind == lush::core::TokenKind::EndOfFile) {
        return lex.tokens.size() - 1;
    }
    return lex.tokens.size();
}

struct Fixture {
    Source source;
    LexResult lex;
    ParseResult result;

    explicit Fixture(std::string text, ParseOptions options = ParseOptions::Script)
        : source(std::move(text)), lex(lexText(source)),
          result(lush::core::parse(lex, options)) {}
};

struct WordFixture {
    Source source;
    LexResult lex;
    ExprParseResult result;

    explicit WordFixture(std::string text, WordEnd end,
                         ParseOptions options = ParseOptions::Script)
        : source(std::move(text)), lex(lexText(source)),
          result(lush::core::parseWord(
              lex, TokenRange{0, eofIndex(lex)}, end, options)) {}
};

struct ExprFixture {
    Source source;
    LexResult lex;
    ExprParseResult result;

    explicit ExprFixture(std::string text, Boundary boundary,
                         TokenRange range = {0, 0})
        : source(std::move(text)), lex(lexText(source)) {
        if (range.end == 0) {
            range = TokenRange{0, eofIndex(lex)};
        }
        result = lush::core::parseExpression(lex, range, boundary,
                                             ParseOptions::Script);
    }
};

void expectOk(const Fixture& fixture) {
    LUSH_CHECK(fixture.result.status == ParseStatus::Ok);
    LUSH_CHECK(fixture.result.program != nullptr);
}

void expectError(const Fixture& fixture, ErrorCode code) {
    LUSH_CHECK(fixture.result.status == ParseStatus::Error);
    LUSH_CHECK(fixture.result.diagnostic.has_value());
    if (!fixture.result.diagnostic) {
        return;
    }
    LUSH_CHECK(fixture.result.diagnostic->code == code);
}

void expectErrorAt(const Fixture& fixture, ErrorCode code, std::size_t line,
                   std::size_t column) {
    expectError(fixture, code);
    if (!fixture.result.diagnostic) {
        return;
    }
    const lush::core::LineCol position =
        fixture.source.lineCol(fixture.result.diagnostic->span.start);
    LUSH_CHECK_EQ(position.line, line);
    LUSH_CHECK_EQ(position.column, column);
}

void expectIncomplete(const Fixture& fixture, IncompleteReason reason) {
    LUSH_CHECK(fixture.result.status == ParseStatus::Incomplete);
    LUSH_CHECK(fixture.result.incompleteReason.has_value());
    if (fixture.result.incompleteReason) {
        LUSH_CHECK(*fixture.result.incompleteReason == reason);
    }
}

std::string dump(const Fixture& fixture) {
    if (!fixture.result.program) {
        return {};
    }
    return lush::core::dumpAst(*fixture.result.program, fixture.source);
}

std::string dumpWord(const WordFixture& fixture) {
    if (!fixture.result.expression) {
        return {};
    }
    return lush::core::dumpExpr(*fixture.result.expression, fixture.source);
}

void expectWordError(const WordFixture& fixture, ErrorCode code,
                     std::size_t column) {
    LUSH_CHECK(fixture.result.status == ParseStatus::Error);
    LUSH_CHECK(fixture.result.diagnostic.has_value());
    if (!fixture.result.diagnostic) {
        return;
    }
    LUSH_CHECK(fixture.result.diagnostic->code == code);
    const lush::core::LineCol position =
        fixture.source.lineCol(fixture.result.diagnostic->span.start);
    LUSH_CHECK_EQ(position.column, column);
}

} // namespace

LUSH_TEST(parser_dump_command_point) {
    Fixture fixture("line 0,0");
    expectOk(fixture);
    LUSH_CHECK_EQ(dump(fixture),
                  "Program @1:1..1:9\n"
                  "  Command name=\"line\" @1:1..1:9\n"
                  "    Point bare=true @1:6..1:9\n"
                  "      Int 0 @1:6..1:7\n"
                  "      Int 0 @1:8..1:9\n");
}

LUSH_TEST(parser_dump_for_ref_deref) {
    Fixture fixture("for (i=0; i<n; i++) { echo i }");
    expectOk(fixture);
    const std::string text = dump(fixture);
    LUSH_CHECK(text.find("For @1:1..1:31") != std::string::npos);
    LUSH_CHECK(text.find("Assign @1:6..1:9") != std::string::npos);
    LUSH_CHECK(text.find("IncDec op=\"++\" prefix=false") != std::string::npos);
    LUSH_CHECK(text.find("Block @1:21..1:31") != std::string::npos);

    Fixture ref("a = ref b");
    expectOk(ref);
    LUSH_CHECK(dump(ref).find("RefAssign target=\"a\" source=\"b\"") !=
               std::string::npos);

    Fixture deref("echo *h");
    expectOk(deref);
    LUSH_CHECK(dump(deref).find("Deref @1:6..1:8") != std::string::npos);
    LUSH_CHECK(dump(deref).find("Variable name=\"h\"") != std::string::npos);
}

LUSH_TEST(parser_dump_empty_stmt) {
    Fixture fixture("a = 1;;b = 2");
    expectOk(fixture);
    const std::string text = dump(fixture);
    LUSH_CHECK(text.find("EmptyStmt @1:7..1:8") != std::string::npos);
    LUSH_CHECK(text.find("Assign @1:8..1:13") != std::string::npos);

    Fixture block("{};");
    expectOk(block);
    LUSH_CHECK(dump(block).find("EmptyStmt @1:3..1:4") != std::string::npos);
}

LUSH_TEST(parser_statement_position_is_p001) {
    expectErrorAt(Fixture("(1,2]"), ErrorCode::P001, 1, 1);
    expectErrorAt(Fixture("(1,2,3,4)"), ErrorCode::P001, 1, 1);
    expectErrorAt(Fixture("1++"), ErrorCode::P001, 1, 1);
    expectErrorAt(Fixture("(x)++"), ErrorCode::P001, 1, 1);
}

LUSH_TEST(parser_expression_position_p003_p005) {
    expectErrorAt(Fixture("a = (1,2]"), ErrorCode::P005, 1, 9);
    expectErrorAt(Fixture("a = (1,2,3,4)"), ErrorCode::P003, 1, 5);
}

LUSH_TEST(parser_command_p004_positions) {
    expectErrorAt(Fixture("line a - b"), ErrorCode::P004, 1, 8);
    expectErrorAt(Fixture("line 1, 2"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line 1,2,3,4"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line 1,(2,3)"), ErrorCode::P004, 1, 8);
    expectErrorAt(Fixture("line 1,2,3,(4,5)"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line 1,2,3,"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line a+ b"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line 1?2 3"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line +"), ErrorCode::P004, 1, 6);
}

LUSH_TEST(parser_command_word_end_p002) {
    expectErrorAt(Fixture("line a+"), ErrorCode::P002, 1, 8);
    expectErrorAt(Fixture("line 1?2"), ErrorCode::P002, 1, 9);
    expectErrorAt(Fixture("line -"), ErrorCode::P002, 1, 7);
}

LUSH_TEST(parser_command_internal_missing_operand_p004) {
    expectErrorAt(Fixture("line (a + )"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line f(a + )"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line [1 + ]"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("line (1 ? 2 : )"), ErrorCode::P004, 1, 6);
}

LUSH_TEST(parser_command_trailing_comma_and_closers) {
    expectErrorAt(Fixture("line f(1,)"), ErrorCode::P001, 1, 10);
    expectErrorAt(Fixture("line [1,]"), ErrorCode::P001, 1, 9);
    expectErrorAt(Fixture("line (1,)"), ErrorCode::P001, 1, 9);
    expectErrorAt(Fixture("line (1,2,3,)"), ErrorCode::P001, 1, 13);
    expectErrorAt(Fixture("line 1,2)"), ErrorCode::P001, 1, 9);
    expectErrorAt(Fixture("line 1,2]"), ErrorCode::P001, 1, 9);
    expectErrorAt(Fixture("line (1,2))"), ErrorCode::P001, 1, 11);
    expectErrorAt(Fixture("line (1,2]"), ErrorCode::P005, 1, 10);
}

LUSH_TEST(parser_s009_operator_and_deref_suffix) {
    expectErrorAt(Fixture("x = 1++"), ErrorCode::S009, 1, 6);
    expectErrorAt(Fixture("line 1++"), ErrorCode::S009, 1, 7);
    expectErrorAt(Fixture("x = (*h)++"), ErrorCode::S009, 1, 9);
    expectErrorAt(Fixture("x = *h++"), ErrorCode::P001, 1, 7);
    expectOk(Fixture("y = (x)++"));
}

LUSH_TEST(parser_member_errors) {
    expectErrorAt(Fixture("a = x.ref"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("a = x.def"), ErrorCode::P004, 1, 6);
    expectErrorAt(Fixture("a = x .ref"), ErrorCode::P001, 1, 7);
    expectErrorAt(Fixture("a = x."), ErrorCode::P002, 1, 7);
    expectErrorAt(Fixture("a = x.;"), ErrorCode::P002, 1, 7);
}

LUSH_TEST(parser_else_and_empty_statement) {
    expectOk(Fixture("if (a);"));
    expectOk(Fixture("if (a); else b = 2"));
    expectOk(Fixture("if (a); b = 2"));
    expectOk(Fixture("if (a) b=1; else c=2"));
    expectOk(Fixture("if (a) {} \n else c=2"));
    expectOk(Fixture("if (a) {} b=2"));
    expectOk(Fixture("{ a=1 } b=2"));

    expectErrorAt(Fixture("if (a) b=1;; else c=2"), ErrorCode::P001, 1, 14);
    expectErrorAt(Fixture("if (a) b=1\n; else c=2"), ErrorCode::P001, 2, 3);
    expectErrorAt(Fixture("if (a) {}; else c=2"), ErrorCode::P001, 1, 12);
}

LUSH_TEST(parser_for_boundaries) {
    expectErrorAt(Fixture("for (p.x=0; ; ) ;"), ErrorCode::P001, 1, 7);
    expectErrorAt(Fixture("for (ref=0; ; ) ;"), ErrorCode::S004, 1, 6);
    expectErrorAt(Fixture("for (def=0; ; ) ;"), ErrorCode::S007, 1, 6);
    expectErrorAt(Fixture("for (i=ref j; ; ) ;"), ErrorCode::S004, 1, 8);
    expectErrorAt(Fixture("for (i; ; ) ;"), ErrorCode::P001, 1, 7);
    expectErrorAt(Fixture("for (;; i=i+1) ;"), ErrorCode::P001, 1, 10);
    expectOk(Fixture("for i in a b"));
    expectOk(Fixture("for (;;);"));

    expectIncomplete(Fixture("for (;;)", ParseOptions::Repl),
                     IncompleteReason::NeedStatement);
    expectIncomplete(Fixture("for i in a", ParseOptions::Repl),
                     IncompleteReason::NeedStatement);
    expectErrorAt(Fixture("if (a) { for (;;) }"), ErrorCode::P001, 1, 19);
}

LUSH_TEST(parser_ref_rules) {
    expectErrorAt(Fixture("a = ref"), ErrorCode::S004, 1, 5);
    expectErrorAt(Fixture("a = ref\nf"), ErrorCode::S004, 1, 5);
    expectOk(Fixture("a = ref \\\nf"));
    expectIncomplete(Fixture("a = ref \\\n", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectError(Fixture("a = ref 1 \\\n"), ErrorCode::S004);
    expectError(Fixture("a = ref f + \\\n"), ErrorCode::S004);
    expectError(Fixture("a = ref ; \\\n"), ErrorCode::S004);
    expectIncomplete(Fixture("a = ref \\\n# c\n", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
}

LUSH_TEST(parser_boundary_int) {
    expectErrorAt(Fixture("echo 9223372036854775808"), ErrorCode::L004, 1, 6);
    expectOk(Fixture("x = -9223372036854775808"));
    expectOk(Fixture("x = -(9223372036854775808)"));
    expectOk(Fixture("x = - -9223372036854775808"));
    expectErrorAt(Fixture("x = --9223372036854775808"), ErrorCode::S009, 1, 5);
    expectErrorAt(Fixture("x = 9223372036854775808++"), ErrorCode::S009, 1, 24);
    expectErrorAt(Fixture("x = -9223372036854775808[0]"), ErrorCode::L004, 1, 6);
    expectOk(Fixture("line -9223372036854775808"));
    expectErrorAt(Fixture("line - 9223372036854775808"), ErrorCode::P004, 1, 6);
    expectOk(Fixture("line (- 9223372036854775808)"));

    Fixture fixture("x = -(9223372036854775808)");
    expectOk(fixture);
    const std::string text = dump(fixture);
    LUSH_CHECK(text.find("BoundaryInt @1:7..1:26") != std::string::npos);
    LUSH_CHECK(text.find("Unary op=\"-\" @1:5..1:27") != std::string::npos);
}

LUSH_TEST(parser_repl_incomplete_reasons) {
    expectIncomplete(Fixture("a = (1", ParseOptions::Repl),
                     IncompleteReason::UnclosedParen);
    expectIncomplete(Fixture("{", ParseOptions::Repl),
                     IncompleteReason::UnclosedBrace);
    expectIncomplete(Fixture("a = [1", ParseOptions::Repl),
                     IncompleteReason::UnclosedBracket);
    expectIncomplete(Fixture("if (true) {", ParseOptions::Repl),
                     IncompleteReason::UnclosedBrace);
    expectIncomplete(Fixture("if (true)", ParseOptions::Repl),
                     IncompleteReason::NeedStatement);
    expectIncomplete(Fixture("while (true)", ParseOptions::Repl),
                     IncompleteReason::NeedStatement);
}

LUSH_TEST(parser_repl_trailing_continuation) {
    expectIncomplete(Fixture("a = 1 + \\\n", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectIncomplete(Fixture("a = 1 \\\n", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectIncomplete(Fixture("a = ref \\\n", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectIncomplete(Fixture("a = 1 + \\\n# c\n", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectErrorAt(Fixture("a = 1 + ; \\\n"), ErrorCode::P002, 1, 9);
}

LUSH_TEST(parser_repl_p002_not_incomplete) {
    expectErrorAt(Fixture("a =", ParseOptions::Repl), ErrorCode::P002, 1, 4);
    expectErrorAt(Fixture("a = 1 +", ParseOptions::Repl), ErrorCode::P002, 1, 8);
    expectErrorAt(Fixture("if (a) else b = 2", ParseOptions::Repl),
                  ErrorCode::P001, 1, 8);
}

LUSH_TEST(parser_l005_incomplete) {
    expectIncomplete(Fixture("a = ref \\", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectIncomplete(Fixture("a = 1 + \\", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectIncomplete(Fixture("a = 1 \\", ParseOptions::Repl),
                     IncompleteReason::TrailingContinuation);
    expectErrorAt(Fixture("a = ) \\", ParseOptions::Repl), ErrorCode::P001, 1, 5);
    expectErrorAt(Fixture("a = 1 \\", ParseOptions::Script), ErrorCode::L005, 1, 7);
}

LUSH_TEST(parser_script_modes) {
    expectOk(Fixture("a = 1 \\\n"));
    expectErrorAt(Fixture("a = 1 + \\\n"), ErrorCode::P002, 2, 1);
    expectErrorAt(Fixture("if (true)"), ErrorCode::P002, 1, 10);
    expectErrorAt(Fixture("a = (1"), ErrorCode::P002, 1, 7);
}

LUSH_TEST(parser_word_end_modes) {
    expectWordError(WordFixture("a+", WordEnd::WordBoundary), ErrorCode::P004, 1);
    expectWordError(WordFixture("a+", WordEnd::InputEnd), ErrorCode::P002, 3);
    expectWordError(WordFixture("1?2", WordEnd::WordBoundary), ErrorCode::P004, 1);
    expectWordError(WordFixture("1?2", WordEnd::InputEnd), ErrorCode::P002, 4);
    expectWordError(WordFixture("1,", WordEnd::InputEnd), ErrorCode::P004, 1);
    expectWordError(WordFixture("+", WordEnd::InputEnd), ErrorCode::P004, 1);
    expectWordError(WordFixture("(),", WordEnd::InputEnd), ErrorCode::P004, 1);
    expectWordError(WordFixture("*h[0]", WordEnd::InputEnd), ErrorCode::P001, 3);
    expectWordError(WordFixture("f(1,)", WordEnd::InputEnd), ErrorCode::P001, 5);
    expectWordError(WordFixture("(1,)", WordEnd::InputEnd), ErrorCode::P001, 4);
    expectWordError(WordFixture("(1,2,3,)", WordEnd::InputEnd), ErrorCode::P001, 8);
    expectWordError(WordFixture("1,2)", WordEnd::InputEnd), ErrorCode::P001, 4);
    expectWordError(WordFixture("x.", WordEnd::WordBoundary), ErrorCode::P004, 2);
    expectWordError(WordFixture("x.", WordEnd::InputEnd), ErrorCode::P002, 3);
}

LUSH_TEST(parser_word_success) {
    WordFixture point("1,2", WordEnd::InputEnd);
    LUSH_CHECK(point.result.status == ParseStatus::Ok);
    LUSH_CHECK(point.result.expression != nullptr);
    LUSH_CHECK_EQ(dumpWord(point),
                  "Point bare=true @1:1..1:4\n"
                  "  Int 1 @1:1..1:2\n"
                  "  Int 2 @1:3..1:4\n");

    WordFixture expr("a+1", WordEnd::InputEnd);
    LUSH_CHECK(expr.result.status == ParseStatus::Ok);
}

LUSH_TEST(parser_expression_boundaries) {
    ExprFixture closing("(1 + )", Boundary::ClosingParen, TokenRange{1, 3});
    LUSH_CHECK(closing.result.status == ParseStatus::Error);
    LUSH_CHECK(closing.result.diagnostic.has_value());
    if (closing.result.diagnostic) {
        LUSH_CHECK(closing.result.diagnostic->code == ErrorCode::P001);
        const auto position = closing.source.lineCol(closing.result.diagnostic->span.start);
        LUSH_CHECK_EQ(position.column, 6);
    }

    ExprFixture word("1 +", Boundary::WordBoundary, TokenRange{0, 2});
    LUSH_CHECK(word.result.status == ParseStatus::Error);
    LUSH_CHECK(word.result.diagnostic.has_value());
    if (word.result.diagnostic) {
        LUSH_CHECK(word.result.diagnostic->code == ErrorCode::P004);
    }

    ExprFixture input("1 +", Boundary::InputEnd, TokenRange{0, 2});
    LUSH_CHECK(input.result.status == ParseStatus::Error);
    LUSH_CHECK(input.result.diagnostic.has_value());
    if (input.result.diagnostic) {
        LUSH_CHECK(input.result.diagnostic->code == ErrorCode::P002);
    }
}

LUSH_TEST(parser_split_command_words) {
    const std::string text = "line (0, 0) (1, 1)";
    Source source(text);
    LexResult lex = lexText(source);
    const auto words = lush::core::splitCommandWords(
        lex.tokens, 1, eofIndex(lex));
    LUSH_CHECK_EQ(words.size(), std::size_t{2});
    if (words.size() == 2) {
        LUSH_CHECK_EQ(lex.tokens[words[0].range.begin].text, std::string("("));
        LUSH_CHECK(words[0].end == WordEnd::WordBoundary);
        LUSH_CHECK(words[1].end == WordEnd::InputEnd);
    }

    const std::string text2 = "line 1, 2";
    Source source2(text2);
    LexResult lex2 = lexText(source2);
    const auto words2 = lush::core::splitCommandWords(
        lex2.tokens, 1, eofIndex(lex2));
    LUSH_CHECK_EQ(words2.size(), std::size_t{2});
    if (words2.size() == 2) {
        LUSH_CHECK_EQ(lex2.tokens[words2[0].range.begin].text, std::string("1"));
        LUSH_CHECK_EQ(lex2.tokens[words2[1].range.begin].text, std::string("2"));
    }
}

LUSH_TEST(lexer_continuation_metadata) {
    const std::string text = "a = 1 \\\n";
    Source source(text);
    LexResult lex = lexText(source);
    LUSH_CHECK(!lex.diagnostic.has_value());
    LUSH_CHECK(lex.endsWithContinuation);
    LUSH_CHECK_EQ(lex.lastContinuationOffset, std::size_t{6});
    LUSH_CHECK_EQ(lex.endOffset, text.size());

    const std::string text2 = "a = 1 \\\n2";
    Source source2(text2);
    LexResult lex2 = lexText(source2);
    LUSH_CHECK(!lex2.endsWithContinuation);
    LUSH_CHECK_EQ(lex2.lastContinuationOffset, std::size_t{6});

    const std::string text3 = "a = 1 \\\n# c\n";
    Source source3(text3);
    LexResult lex3 = lexText(source3);
    LUSH_CHECK(lex3.endsWithContinuation);

    const std::string text4 = "\\";
    Source source4(text4);
    LexResult lex4 = lexText(source4);
    LUSH_CHECK(lex4.diagnostic.has_value());
    if (lex4.diagnostic) {
        LUSH_CHECK(lex4.diagnostic->code == ErrorCode::L005);
    }
    LUSH_CHECK(lex4.endsWithContinuation);
    LUSH_CHECK_EQ(lex4.lastContinuationOffset, std::size_t{0});
    LUSH_CHECK_EQ(lex4.endOffset, std::size_t{1});
}

#include "lush/lush.h"

#include "lush/core/ast.h"
#include "lush/core/parser.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

void setupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void printVersion() {
    std::cout << "lush " << lush::version() << '\n';
}

void printHelp() {
    std::cout
        << "usage: lush [options]\n"
        << "       lush --dump-tokens <file>\n"
        << "       lush --dump-tokens -e \"<source>\"\n"
        << "       lush --dump-ast <file>\n"
        << "       lush --dump-ast -e \"<source>\"\n"
        << "\n"
        << "options:\n"
        << "  -v, --version        显示版本\n"
        << "  -h, --help           显示帮助\n"
        << "  --dump-tokens <file> 输出文件中的 token 流\n"
        << "  --dump-tokens -e ... 输出一段源码的 token 流\n"
        << "  --dump-ast <file>    输出文件中的 AST\n"
        << "  --dump-ast -e ...    输出一段源码的 AST\n";
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        return false;
    }

    out = buffer.str();
    return true;
}

std::string escapeDumpText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    constexpr char kHex[] = "0123456789ABCDEF";

    for (unsigned char value : text) {
        switch (value) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\0':
            escaped += "\\0";
            break;
        default:
            if (value < 0x20U || value == 0x7FU) {
                escaped += "\\u00";
                escaped.push_back(kHex[(value >> 4) & 0x0FU]);
                escaped.push_back(kHex[value & 0x0FU]);
            } else {
                escaped.push_back(static_cast<char>(value));
            }
            break;
        }
    }

    return escaped;
}

int dumpTokens(const std::string& text) {
    lush::core::Source source(text);
    lush::core::Lexer lexer(source);
    lush::core::LexResult result = lexer.tokenize();

    for (const lush::core::Token& token : result.tokens) {
        const lush::core::LineCol position = source.lineCol(token.offset);
        std::string kind(lush::core::tokenKindName(token.kind));
        if (kind.size() < 13) {
            kind.append(13 - kind.size(), ' ');
        }

        std::cout << kind << "@" << position.line << ':' << position.column
                  << "  offset=" << token.offset << "  length=" << token.length
                  << "  ws=" << (token.wsBefore ? 1 : 0) << "  \""
                  << escapeDumpText(token.text) << "\"\n";
    }

    if (result.diagnostic) {
        std::cerr << lush::core::formatDiagnostic(source, *result.diagnostic);
        return 1;
    }

    return 0;
}

int dumpAst(const std::string& text) {
    lush::core::Source source(text);
    lush::core::Lexer lexer(source);
    lush::core::LexResult lex = lexer.tokenize();

    if (lex.diagnostic) {
        std::cerr << lush::core::formatDiagnostic(source, *lex.diagnostic);
        return 1;
    }

    lush::core::ParseResult result =
        lush::core::parse(lex, lush::core::ParseOptions::Script);

    if (result.status != lush::core::ParseStatus::Ok || !result.program) {
        if (result.diagnostic) {
            std::cerr << lush::core::formatDiagnostic(source, *result.diagnostic);
        } else {
            std::cerr << "error: 解析失败\n";
        }
        return 1;
    }

    std::cout << lush::core::dumpAst(*result.program, source);
    return 0;
}

bool hasOption(int argc, char** argv, std::string_view option) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "-e") {
            ++index;
            continue;
        }
        if (std::string_view(argv[index]) == option) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    setupConsole();

    const bool wantsDumpTokens = hasOption(argc, argv, "--dump-tokens");
    const bool wantsDumpAst = hasOption(argc, argv, "--dump-ast");
    if (wantsDumpTokens && wantsDumpAst) {
        std::cerr << "--dump-tokens and --dump-ast are mutually exclusive\n";
        return 2;
    }

    if (argc == 1) {
        std::cout << "lush " << lush::version()
                  << " (M2 parser; REPL not implemented yet)\n";
        return 0;
    }

    if (std::string_view(argv[1]) == "--dump-tokens") {
        if (argc == 3 && std::string_view(argv[2]) != "-e") {
            std::string text;
            if (!readFile(argv[2], text)) {
                std::cerr << "cannot read file: " << argv[2] << '\n';
                return 2;
            }
            return dumpTokens(text);
        }

        if (argc == 4 && std::string_view(argv[2]) == "-e") {
            return dumpTokens(std::string(argv[3]));
        }

        std::cerr << "usage: lush --dump-tokens <file>\n"
                  << "       lush --dump-tokens -e \"<source>\"\n";
        return 2;
    }

    if (std::string_view(argv[1]) == "--dump-ast") {
        if (argc == 3 && std::string_view(argv[2]) != "-e") {
            std::string text;
            if (!readFile(argv[2], text)) {
                std::cerr << "cannot read file: " << argv[2] << '\n';
                return 2;
            }
            return dumpAst(text);
        }

        if (argc == 4 && std::string_view(argv[2]) == "-e") {
            return dumpAst(std::string(argv[3]));
        }

        std::cerr << "usage: lush --dump-ast <file>\n"
                  << "       lush --dump-ast -e \"<source>\"\n";
        return 2;
    }

    if (argc > 2) {
        std::cerr << "too many arguments\n";
        return 2;
    }

    const std::string_view arg = argv[1];

    if (arg == "--version" || arg == "-v") {
        printVersion();
        return 0;
    }

    if (arg == "--help" || arg == "-h") {
        printHelp();
        return 0;
    }

    std::cerr << "unknown option: " << arg << '\n';
    return 2;
}

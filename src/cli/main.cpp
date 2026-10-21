#include "lush/lush.h"

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
        << "\n"
        << "options:\n"
        << "  -v, --version        显示版本\n"
        << "  -h, --help           显示帮助\n"
        << "  --dump-tokens <file> 输出文件中的 token 流\n"
        << "  --dump-tokens -e ... 输出一段源码的 token 流\n";
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

} // namespace

int main(int argc, char** argv) {
    setupConsole();

    if (argc == 1) {
        std::cout << "lush " << lush::version()
                  << " (M1 lexer; REPL not implemented yet)\n";
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

#include "cli/console_sink.h"
#include "cli/repl.h"
#include "lush/command/registry.h"
#include "lush/core/ast.h"
#include "lush/core/parser.h"
#include "lush/driver/driver.h"
#include "lush/lush.h"
#include "reference/reference_host.h"

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
        << "usage: lush\n"
        << "       lush <file>\n"
        << "       lush -e \"<source>\"\n"
        << "       lush --dump-tokens <file>\n"
        << "       lush --dump-tokens -e \"<source>\"\n"
        << "       lush --dump-ast <file>\n"
        << "       lush --dump-ast -e \"<source>\"\n"
        << "\n"
        << "options:\n"
        << "  -v, --version        显示版本\n"
        << "  -h, --help           显示帮助\n"
        << "  -e ...               执行一段源码\n"
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

void printDriverDiagnostic(const lush::driver::DriverResult& result) {
    if (!result.diagnostic) {
        return;
    }
    if (result.source) {
        std::cerr << lush::core::formatDiagnostic(*result.source,
                                                  *result.diagnostic);
        return;
    }
    std::cerr << "error[" << lush::core::errorCodeName(result.diagnostic->code)
              << "]: " << result.diagnostic->message << '\n';
}

int runSource(std::string text) {
    lush::core::Env env;
    lush::cli::ConsoleSink sink;
    lush::reference::ReferenceHost host(std::cin, sink);
    lush::driver::Driver driver(env, host.registry(), sink);

    const lush::driver::DriverResult result = driver.runScript(std::move(text));
    if (result.status == lush::driver::DriverStatus::Error) {
        printDriverDiagnostic(result);
        return 1;
    }
    return 0;
}

int runFile(const std::string& path) {
    std::string text;
    if (!readFile(path, text)) {
        std::cerr << "cannot read file: " << path << '\n';
        return 2;
    }
    return runSource(std::move(text));
}

} // namespace

int main(int argc, char** argv) {
    setupConsole();

    if (argc == 1) {
        lush::core::Env env;
        lush::cli::ConsoleSink sink;
        lush::reference::ReferenceHost host(std::cin, sink);
        lush::driver::Driver driver(env, host.registry(), sink);
        return lush::cli::runRepl(driver);
    }

    const std::string_view arg = argv[1];

    if (argc >= 3 &&
        ((arg == "--dump-tokens" &&
          std::string_view(argv[2]) == "--dump-ast") ||
         (arg == "--dump-ast" &&
          std::string_view(argv[2]) == "--dump-tokens"))) {
        std::cerr << "--dump-tokens and --dump-ast are mutually exclusive\n";
        return 2;
    }

    if (arg == "--version" || arg == "-v") {
        if (argc != 2) {
            std::cerr << "too many arguments\n";
            return 2;
        }
        printVersion();
        return 0;
    }

    if (arg == "--help" || arg == "-h") {
        if (argc != 2) {
            std::cerr << "too many arguments\n";
            return 2;
        }
        printHelp();
        return 0;
    }

    if (arg == "--dump-tokens") {
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

    if (arg == "--dump-ast") {
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

    if (arg == "-e") {
        if (argc != 3) {
            std::cerr << "usage: lush -e \"<source>\"\n";
            return 2;
        }
        return runSource(std::string(argv[2]));
    }

    if (argc > 2) {
        std::cerr << "too many arguments\n";
        return 2;
    }

    if (!arg.empty() && arg.front() == '-') {
        std::cerr << "unknown option: " << arg << '\n';
        return 2;
    }

    return runFile(std::string(arg));
}

#include "lush/lush.h"

#include <iostream>
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
        << "\n"
        << "options:\n"
        << "  -v, --version  显示版本\n"
        << "  -h, --help     显示帮助\n";
}

} // namespace

int main(int argc, char** argv) {
    setupConsole();

    if (argc == 1) {
        std::cout << "lush " << lush::version()
                  << " (M0 skeleton; REPL not implemented yet)\n";
        return 0;
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

#include "cli/repl.h"

#include "lush/core/format.h"

#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace lush::cli {
namespace {

volatile std::sig_atomic_t g_interrupted = 0;

#ifdef _WIN32
BOOL WINAPI consoleCtrlHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        g_interrupted = 1;
        return TRUE;
    }
    return FALSE;
}
#else
void sigintHandler(int) {
    g_interrupted = 1;
}
#endif

void installInterruptHandler() {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    struct sigaction action {};
    action.sa_handler = sigintHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
#endif
}

void writePrompt(std::string_view prompt) {
    std::cout.write(prompt.data(), static_cast<std::streamsize>(prompt.size()));
    std::cout.flush();
}

void printDiagnostic(const driver::DriverResult& result) {
    if (!result.diagnostic) {
        return;
    }
    if (result.source) {
        std::cerr << core::formatDiagnostic(*result.source, *result.diagnostic);
    } else {
        std::cerr << "error[" << core::errorCodeName(result.diagnostic->code)
                  << "]: " << result.diagnostic->message << '\n';
    }
}

std::string promptFor(const driver::DriverResult& result) {
    switch (result.status) {
    case driver::DriverStatus::NeedMore:
        return " ...> ";
    case driver::DriverStatus::NeedInput:
        if (result.input) {
            return result.input->prompt + ": ";
        }
        return " ...> ";
    case driver::DriverStatus::Error:
        if (result.sessionActive && result.input) {
            return result.input->prompt + ": ";
        }
        return "lush> ";
    case driver::DriverStatus::Done:
    case driver::DriverStatus::Cancelled:
        return "lush> ";
    }
    return "lush> ";
}

void printCancellation(const driver::DriverResult& result, bool showPrompts) {
    if (!showPrompts) {
        return;
    }
    if (result.cancelReason == driver::CancelReason::EmptyRequired ||
        result.cancelReason == driver::CancelReason::Session) {
        std::cout << "已取消\n";
    }
}

} // namespace

bool stdinIsTty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

int runRepl(driver::Driver& driver) {
    installInterruptHandler();
    const bool showPrompts = stdinIsTty();

    if (showPrompts) {
        writePrompt("lush> ");
    }

    std::string line;
    for (;;) {
        g_interrupted = 0;
        if (!std::getline(std::cin, line)) {
            if (g_interrupted != 0) {
                std::cin.clear();
                if (driver.sessionActive() || driver.pendingInput()) {
                    driver::DriverResult cancelled = driver.cancel();
                    printCancellation(cancelled, showPrompts);
                }
                if (showPrompts) {
                    writePrompt("lush> ");
                }
                continue;
            }

            driver::DriverResult final = driver.endInput();
            if (final.status == driver::DriverStatus::Error) {
                printDiagnostic(final);
                return 1;
            }
            return 0;
        }

        if (g_interrupted != 0) {
            std::cin.clear();
            if (driver.sessionActive() || driver.pendingInput()) {
                driver::DriverResult cancelled = driver.cancel();
                printCancellation(cancelled, showPrompts);
            }
            if (showPrompts) {
                writePrompt("lush> ");
            }
            continue;
        }

        driver::DriverResult result = driver.feedLine(line);
        if (result.status == driver::DriverStatus::Error) {
            printDiagnostic(result);
        }
        if (result.status == driver::DriverStatus::Cancelled) {
            printCancellation(result, showPrompts);
        }
        if (showPrompts) {
            writePrompt(promptFor(result));
        }
    }
}

} // namespace lush::cli

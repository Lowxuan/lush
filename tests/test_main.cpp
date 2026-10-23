#include "test_harness.h"

#include <exception>
#include <iostream>
#include <utility>

namespace lush::test {

int failures = 0;

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar(std::string name, std::function<void()> fn) {
    registry().push_back(TestCase{std::move(name), std::move(fn)});
}

void fail(const char* file, int line, const std::string& message) {
    ++failures;
    std::cerr << file << ':' << line << ": " << message << '\n';
}

} // namespace lush::test

int main() {
    int failedTests = 0;
    const auto& tests = lush::test::registry();

    for (const auto& test : tests) {
        const int before = lush::test::failures;

        try {
            test.fn();
        } catch (const std::exception& e) {
            ++lush::test::failures;
            std::cerr << "uncaught exception: " << e.what() << '\n';
        } catch (...) {
            ++lush::test::failures;
            std::cerr << "uncaught exception\n";
        }

        if (lush::test::failures != before) {
            ++failedTests;
            std::cerr << "[FAIL] " << test.name << std::endl;
        } else {
            std::cout << "[ OK ] " << test.name << std::endl;
        }
    }

    std::cout << tests.size() << " tests, " << failedTests << " failed"
              << std::endl;
    return failedTests == 0 ? 0 : 1;
}

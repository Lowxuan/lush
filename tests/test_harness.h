#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace lush::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(std::string name, std::function<void()> fn);
};

extern int failures;

void fail(const char* file, int line, const std::string& message);

template <typename A, typename B>
void checkEq(const char* file, int line, const A& lhs, const B& rhs,
             const char* lhsText, const char* rhsText) {
    if (lhs == rhs) {
        return;
    }

    std::ostringstream out;
    out << lhsText << " == " << rhsText << " failed: "
        << lhs << " != " << rhs;
    fail(file, line, out.str());
}

} // namespace lush::test

#define LUSH_TEST(name)                                                \
    static void name();                                                \
    static ::lush::test::Registrar name##_registrar(#name, &name);     \
    static void name()

#define LUSH_CHECK(condition)                                          \
    do {                                                               \
        if (!(condition)) {                                            \
            ::lush::test::fail(__FILE__, __LINE__,                     \
                               "CHECK failed: " #condition);           \
        }                                                              \
    } while (0)

#define LUSH_CHECK_EQ(lhs, rhs)                                        \
    do {                                                               \
        ::lush::test::checkEq(__FILE__, __LINE__, (lhs), (rhs),        \
                              #lhs, #rhs);                             \
    } while (0)

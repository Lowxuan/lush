#include "test_harness.h"

#include "lush/core/text.h"

#include <string>
#include <string_view>

namespace {

using lush::core::asciiEqualsIgnoreCase;
using lush::core::asciiLower;

} // namespace

LUSH_TEST(text_ascii_lower) {
    LUSH_CHECK(asciiLower('A') == 'a');
    LUSH_CHECK(asciiLower('Z') == 'z');
    LUSH_CHECK(asciiLower('a') == 'a');
    LUSH_CHECK(asciiLower('7') == '7');
    LUSH_CHECK_EQ(asciiLower("AbC-1_z"), std::string("abc-1_z"));

    // 非 ASCII 字节必须保持原样；这里用的是 UTF-8 的两个字节。
    LUSH_CHECK_EQ(asciiLower("\xC3\x84"), std::string("\xC3\x84"));
}

LUSH_TEST(text_ascii_equals_ignore_case) {
    LUSH_CHECK(asciiEqualsIgnoreCase("", ""));
    LUSH_CHECK(asciiEqualsIgnoreCase("Foo", "fOO"));
    LUSH_CHECK(asciiEqualsIgnoreCase("A1_z", "a1_Z"));
    LUSH_CHECK(!asciiEqualsIgnoreCase("Foo", "FoO "));
    LUSH_CHECK(!asciiEqualsIgnoreCase("a", "A1"));
    LUSH_CHECK(!asciiEqualsIgnoreCase("\xC3\x84", "\xC3\xA4"));
}

#include "test_harness.h"

#include "eval_test_utils.h"

#include <string>

namespace {

using lush::core::Env;
using lush::core::ErrorCode;
using lush::test::expectError;
using lush::test::expectOutput;
using lush::test::runScript;

LUSH_TEST(G01) {
    Env env;
    const auto result = runScript(env, R"(a = 3.2343
b = 2
echo a
echo b
echo b/4
echo b/4.0
echo 2/4
echo -3/2
echo 2*3+4
)");
    expectOutput(result, "3.2343\n2\n0\n0.5\n0\n-1\n10\n");
}

LUSH_TEST(G02) {
    Env env;
    const auto result = runScript(env, R"(echo (1 + 2 * 3)
echo ((1 + 2) * 3)
echo (true ? "y" : "n")
echo (1 < 2 && 2 < 3)
)");
    expectOutput(result, "7\n9\ny\ntrue\n");
}

LUSH_TEST(G03) {
    Env env;
    const auto result = runScript(env, R"(s = "a\tb"
echo s
echo ("x" + "y")
echo s[0]
echo len("中文")
echo ("中" == "中")
)");
    expectOutput(result, "a\tb\nxy\na\n2\ntrue\n");
}

LUSH_TEST(G04) {
    Env env;
    const auto result = runScript(env, R"(a = [1,[2,3]]
b = a
b[1][0] = 9
echo a
echo b
)");
    expectOutput(result, "[1, [2, 3]]\n[1, [9, 3]]\n");
}

LUSH_TEST(G05) {
    Env env;
    const auto result = runScript(env, R"(a = [1,2]
h = ref a
h[0] = 7
echo a
h = 100
echo a
echo h
)");
    expectOutput(result, "[7, 2]\n[7, 2]\n100\n");
}

LUSH_TEST(G06) {
    Env env;
    const auto result = runScript(env, R"(a = [1,2]
h = ref a
*h = [5,6]
echo a
)");
    expectOutput(result, "[5, 6]\n");
}

LUSH_TEST(G07) {
    Env env;
    const auto result = runScript(env, R"(r = 0
for i in [10,20] {
    r = ref i
}
echo *r
)");
    expectOutput(result, "20\n");
}

LUSH_TEST(G08) {
    Env env;
    const auto result = runScript(env, R"(p = (1,2)
echo p
echo p[0]
echo p.x
p[1] = 9
echo p
echo (p + (1,1))
echo -p
)");
    expectOutput(result,
                 "(1, 2)\n1\n1\n(1, 9)\n(2, 10)\n(-1, -9)\n");
}

LUSH_TEST(G09) {
    Env env;
    const auto result = runScript(env, R"(q = (1,2,3)
echo q
echo q.z
echo len(q)
)");
    expectOutput(result, "(1, 2, 3)\n3\n3\n");
}

LUSH_TEST(G10) {
    Env env;
    const auto result = runScript(env, R"(p = (1,2)
echo (p * 2)
echo (2 * p)
echo (p / 2)
)");
    expectOutput(result, "(2, 4)\n(2, 4)\n(0.5, 1)\n");
}

LUSH_TEST(G17) {
    Env env;
    const auto result = runScript(env, R"(x = 1
if (true) {
    x = 2
    y = 3
}
echo x
echo y
)");
    LUSH_CHECK_EQ(result.output, std::string("2\n"));
    expectError(result, ErrorCode::R001, 7, 6);
}

LUSH_TEST(G18) {
    Env env;
    const auto result = runScript(env, R"(for i in [1] {
    echo i
}
echo i
)");
    LUSH_CHECK_EQ(result.output, std::string("1\n"));
    expectError(result, ErrorCode::R001, 4, 6);
}

LUSH_TEST(G19) {
    Env env;
    const auto result = runScript(env, R"(a = [1,2]
for x in a {
    x = 99
}
echo a
)");
    expectOutput(result, "[1, 2]\n");
}

LUSH_TEST(G26) {
    Env env;
    const auto result = runScript(env, "p = (1,2,3,4)\n");
    expectError(result, ErrorCode::P003, 1, 5);
}

LUSH_TEST(G27) {
    Env env;
    const auto result = runScript(env, "if (1) echo \"x\"\n");
    expectError(result, ErrorCode::R002, 1, 5);
}

LUSH_TEST(G28) {
    {
        Env env;
        const auto result = runScript(env, R"(echo (false && (1/0) > 0)
echo (true || (1/0) > 0)
echo 1/0
)");
        LUSH_CHECK_EQ(result.output, std::string("false\ntrue\n"));
        expectError(result, ErrorCode::R004, 3, 7);
    }
    {
        Env env;
        const auto result = runScript(env, "x = 9223372036854775807 + 1\n");
        expectError(result, ErrorCode::R005, 1, 25);
    }
}

LUSH_TEST(G29) {
    Env env;
    const auto result = runScript(env, R"(a = 1 + \
2
echo a
echo 1 \
2
)");
    expectOutput(result, "3\n1 2\n");
}

LUSH_TEST(G30) {
    Env env;
    const auto result = runScript(env, R"(if (true)
{
    echo 1
}
else
{
    echo 2
}
)");
    expectOutput(result, "1\n");
}

LUSH_TEST(G31) {
    Env env;
    const auto result = runScript(env, "a = 1; b = 2; echo a+b\n");
    expectOutput(result, "3\n");
}

LUSH_TEST(G32) {
    Env env;
    const auto result = runScript(env, R"(echo type(1)
echo type(1.0)
echo type([1])
echo type((1,2))
echo len("中文")
)");
    expectOutput(result, "int\ndouble\nlist\npoint\n2\n");
}

LUSH_TEST(G33) {
    Env env;
    const auto result = runScript(env, R"(p = point(1,2)
q = point([3,4])
echo p
echo q
echo point((5,6))
)");
    expectOutput(result, "(1, 2)\n(3, 4)\n(5, 6)\n");
}

LUSH_TEST(G35) {
    Env env;
    const auto result = runScript(env, "x = 1\ny = ref x\nz = ref y\n");
    expectError(result, ErrorCode::R008, 3, 9);
}

LUSH_TEST(G36) {
    Env env;
    const auto result = runScript(env, "x = 1\n*x = 2\n");
    expectError(result, ErrorCode::R007, 2, 1);
}

LUSH_TEST(G38) {
    Env env;
    const auto result = runScript(env, R"(echo type(1)
echo type(1.0)
echo type("a")
echo type(true)
echo type([1])
echo type((1,2))
)");
    expectOutput(result, "int\ndouble\nstring\nbool\nlist\npoint\n");
}

LUSH_TEST(G39) {
    Env env;
    const auto result = runScript(env, R"(echo type(4.4)(2)
echo type(1)(2.9)
echo type(true)(0)
echo type("x")(5)
echo type([1])((2,3))
echo type((1,2))([3,4])
)");
    expectOutput(result, "2\n2\nfalse\n5\n[2, 3]\n(3, 4)\n");
}

LUSH_TEST(G40) {
    Env env;
    const auto result = runScript(env, R"(echo (type(1) == int)
echo (type(1) == double)
echo (type(4.4) == double)
echo (type(4.4) == type(2.0))
echo (type(1) == type(2))
)");
    expectOutput(result, "true\nfalse\ntrue\ntrue\ntrue\n");
}

LUSH_TEST(G41) {
    Env env;
    const auto result = runScript(env, R"(t = type(1)
echo t(2.5)
echo (t == int)
f = len
echo f([1,2,3])
echo [int, len]
)");
    expectOutput(result, "2\ntrue\n3\n[int, len]\n");
}

LUSH_TEST(G42) {
    Env env;
    const auto result = runScript(env, R"(echo type(len)
echo type(int)
echo type(type)
echo type(type(1))
echo (type(int) == callable)
echo (type(1) == type)
)");
    expectOutput(result, "callable\ncallable\ncallable\ncallable\ntrue\nfalse\n");
}

LUSH_TEST(G43) {
    Env env;
    const auto result = runScript(env, R"(echo bool(0)
echo bool(1)
echo bool(0.0)
echo bool("")
echo bool("x")
echo bool([])
echo bool([1])
echo list("ab")
echo list((1,2,3))
)");
    expectOutput(result,
                 "false\ntrue\nfalse\nfalse\ntrue\nfalse\ntrue\n"
                 "[\"a\", \"b\"]\n[1, 2, 3]\n");
}

LUSH_TEST(G44) {
    Env env;
    const auto result = runScript(env, "p = (1,2)\necho p(1,2)\n");
    expectError(result, ErrorCode::R012, 2, 6);
}

LUSH_TEST(G45) {
    Env env;
    const auto result = runScript(env, "echo (int + 1)\n");
    expectError(result, ErrorCode::R002, 1, 11);
}

LUSH_TEST(G46) {
    Env env;
    const auto result = runScript(env, "x = type(4.4) (2)\n");
    expectError(result, ErrorCode::P001, 1, 15);
}

LUSH_TEST(G47) {
    Env env;
    const auto result = runScript(env, "echo type(1)(\"x\")\n");
    expectError(result, ErrorCode::R013, 1, 14);
}

LUSH_TEST(G48) {
    Env env;
    const auto result = runScript(env, "echo type(4.4)(2)(3)\n");
    expectError(result, ErrorCode::R012, 1, 6);
}

LUSH_TEST(G49) {
    {
        Env env;
        const auto result = runScript(env, R"(echo -9223372036854775808
echo -(9223372036854775808)
)");
        expectOutput(result,
                     "-9223372036854775808\n-9223372036854775808\n");
    }
    {
        Env env;
        const auto result = runScript(env, "echo 9223372036854775808\n");
        expectError(result, ErrorCode::L004, 1, 6);
    }
    {
        Env env;
        const auto result = runScript(env, "x = 0 - 9223372036854775808\n");
        expectError(result, ErrorCode::L004, 1, 9);
    }
}

LUSH_TEST(G50) {
    Env env;
    const auto result =
        runScript(env, "x = 1\nz = 2\ny = ref x\nx = ref z\n");
    expectError(result, ErrorCode::R008, 4, 1);
}

LUSH_TEST(G51) {
    Env env;
    const auto result = runScript(env, "x = 1\ny = ref x\nx = 5\necho y\n");
    expectOutput(result, "5\n");
}

LUSH_TEST(G52) {
    Env env;
    const auto result = runScript(env, "h = 1\nh = ref h\n");
    expectError(result, ErrorCode::R008, 2, 9);
}

} // namespace

#include "test_harness.h"

#include "eval_test_utils.h"

#include "lush/core/value.h"

#include <cstdint>
#include <string>

namespace {

using lush::core::BuiltinCatalog;
using lush::core::Env;
using lush::core::ErrorCode;
using lush::core::FunctionSignature;
using lush::core::Name;
using lush::core::ValidationOptions;
using lush::core::Value;
using lush::test::expectError;
using lush::test::expectErrorCode;
using lush::test::expectNoError;
using lush::test::expectOutput;
using lush::test::findCell;
using lush::test::hasVariable;
using lush::test::intValue;
using lush::test::runScript;
using lush::test::runScriptWithCatalog;

LUSH_TEST(eval_target_first_basic) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[i] = i++\necho a\necho i\n");
    expectOutput(result, "[0, 20]\n1\n");
}

LUSH_TEST(eval_target_first_rhs_index) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[i++] = i\necho a\necho i\n");
    expectOutput(result, "[0, 20]\n1\n");
}

LUSH_TEST(eval_target_first_two_postfix) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[i++] = i++\necho a\necho i\n");
    expectOutput(result, "[0, 20]\n2\n");
}

LUSH_TEST(eval_target_first_rhs_expression) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[i++] = i++ + i\necho a\necho i\n");
    expectOutput(result, "[1, 20]\n2\n");
}

LUSH_TEST(eval_target_first_prefix) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[++i] = i\necho a\necho i\n");
    expectOutput(result, "[10, 1]\n1\n");
}

LUSH_TEST(eval_target_first_multiple_postfix) {
    Env env;
    const auto result = runScript(
        env, "a = [[1,2],[3,4]]\ni = 0\na[i++][i++] = i\necho a\necho i\n");
    expectOutput(result, "[[0, 2], [3, 4]]\n2\n");
}

LUSH_TEST(eval_target_first_prefix_rhs) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[i++] = ++i\necho a\necho i\n");
    expectOutput(result, "[1, 20]\n2\n");
}

LUSH_TEST(eval_target_first_decrement_rhs) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[i++] = i--\necho a\necho i\n");
    expectOutput(result, "[0, 20]\n0\n");
}

LUSH_TEST(eval_target_first_compound_rhs) {
    Env env;
    const auto result = runScript(
        env, "a = [10,20]\ni = 0\na[i++] = i++ + i++\necho a\necho i\n");
    expectOutput(result, "[1, 20]\n3\n");
}

LUSH_TEST(eval_target_first_pending_on_target_error) {
    Env env;
    const auto result =
        runScript(env, "a = [[1]]\ni = 0\na[i++][99] = 2\n");
    expectErrorCode(result, ErrorCode::R003);
    LUSH_CHECK_EQ(intValue(env, "i"), 1);
}

LUSH_TEST(eval_target_first_pending_on_write_error) {
    Env env;
    const auto result =
        runScript(env, "p = (1,2)\ni = 0\np[i++] = \"x\"\n");
    expectErrorCode(result, ErrorCode::R002);
    LUSH_CHECK_EQ(intValue(env, "i"), 1);
    auto cell = findCell(env, "p");
    LUSH_CHECK(cell != nullptr);
    if (cell) {
        LUSH_CHECK_EQ(cell->read().asPoint()[0], 1.0);
        LUSH_CHECK_EQ(cell->read().asPoint()[1], 2.0);
    }
}

LUSH_TEST(eval_target_first_error_priority) {
    {
        Env env;
        const auto result =
            runScript(env, "a = [10,20]\ni = 0\na[99] = i++\n");
        expectError(result, ErrorCode::R003, 3, 3);
        LUSH_CHECK_EQ(intValue(env, "i"), 0);
    }
    {
        Env env;
        const auto result =
            runScript(env, "s = \"ab\"\ni = 0\ns[0] = i++\n");
        expectError(result, ErrorCode::R002, 3, 3);
        LUSH_CHECK_EQ(intValue(env, "i"), 0);
    }
    {
        Env env;
        const auto result =
            runScript(env, "h = 1\ni = 0\n*h = i++\n");
        expectError(result, ErrorCode::R007, 3, 1);
        LUSH_CHECK_EQ(intValue(env, "i"), 0);
    }
    {
        Env env;
        const auto result =
            runScript(env, "p = (1,2)\ni = 0\np.z = i++\n");
        expectError(result, ErrorCode::R003, 3, 3);
        LUSH_CHECK_EQ(intValue(env, "i"), 0);
    }
}

LUSH_TEST(eval_target_first_error_side_effect) {
    Env env;
    const auto result =
        runScript(env, "a = [10,20]\ni = 0\na[i++] = 1/0\n");
    expectError(result, ErrorCode::R004, 3, 11);
    LUSH_CHECK_EQ(intValue(env, "i"), 1);

    auto cell = findCell(env, "a");
    LUSH_CHECK(cell != nullptr);
    if (cell) {
        const auto& items = cell->read().asList().items();
        LUSH_CHECK_EQ(items.size(), 2u);
        LUSH_CHECK_EQ(items[0].asInt(), 10);
        LUSH_CHECK_EQ(items[1].asInt(), 20);
    }
}

LUSH_TEST(eval_simple_variable_creation) {
    {
        Env env;
        const auto result = runScript(env, "a = a\n");
        expectError(result, ErrorCode::R001, 1, 5);
        LUSH_CHECK(!hasVariable(env, "a"));
    }
    {
        Env env;
        const auto result = runScript(env, "a = 1/0\n");
        expectError(result, ErrorCode::R004, 1, 6);
        LUSH_CHECK(!hasVariable(env, "a"));
    }
}

LUSH_TEST(eval_self_postfix) {
    Env env;
    const auto result =
        runScript(env, "i = 0\ni = i++\necho i\ni = 0\ni = i++ + 1\necho i\n");
    expectOutput(result, "0\n1\n");
}

LUSH_TEST(eval_scope_and_block) {
    {
        Env env;
        const auto result =
            runScript(env, "x = 1\n{\n    x = 2\n    y = 3\n}\necho x\n");
        expectOutput(result, "2\n");
        LUSH_CHECK(!hasVariable(env, "y"));
    }
    {
        Env env;
        const auto result = runScript(
            env, "x = 1\n{\n    y = 3\n}\necho y\n");
        expectError(result, ErrorCode::R001, 5, 6);
    }
}

LUSH_TEST(eval_while_and_continue) {
    Env env;
    const auto result = runScript(
        env, "i = 0\nwhile (i < 3) {\n    echo i\n    i = i + 1\n}\n");
    expectOutput(result, "0\n1\n2\n");
}

LUSH_TEST(eval_for_init_outer_scope) {
    Env env;
    const auto result = runScript(
        env, "i = 5\nfor (i = i + 1; i < 8; i++) {\n    echo i\n}\necho i\n");
    expectOutput(result, "6\n7\n5\n");
}

LUSH_TEST(eval_for_variable_not_visible_after) {
    Env env;
    const auto result =
        runScript(env, "for (j = 0; j < 1; j++) {\n}\necho j\n");
    expectError(result, ErrorCode::R001, 3, 6);
}

LUSH_TEST(eval_for_continue_runs_step) {
    Env env;
    const auto result = runScript(
        env, "for (i = 0; i < 3; i++) {\n    if (i == 1) continue\n"
             "    echo i\n}\n");
    expectOutput(result, "0\n2\n");
}

LUSH_TEST(eval_for_in_snapshot) {
    Env env;
    const auto result = runScript(
        env, "a = [1,2]\nfor x in a {\n    x = 99\n}\necho a\n");
    expectOutput(result, "[1, 2]\n");
}

LUSH_TEST(eval_for_in_string) {
    Env env;
    const auto result = runScript(
        env, "for c in \"中文\" {\n    echo c\n}\n");
    expectOutput(result, "中\n文\n");
}

LUSH_TEST(eval_for_in_rejects_point) {
    Env env;
    const auto result = runScript(
        env, "for x in (1,2) {\n    echo x\n}\n");
    expectErrorCode(result, ErrorCode::R002);
}

LUSH_TEST(eval_echo_forms) {
    Env env;
    const auto result = runScript(env, "echo\necho 1\necho(1)\n");
    expectOutput(result, "\n1\n1\n");
}

LUSH_TEST(eval_builtins) {
    Env env;
    const auto result = runScript(
        env,
        "echo type(1)\n"
        "echo type(1.0)\n"
        "echo type(\"a\")\n"
        "echo type(true)\n"
        "echo type([1])\n"
        "echo type((1,2))\n"
        "echo type(int)\n"
        "echo type(len)\n"
        "echo type(type)\n"
        "echo (type(1) == int)\n"
        "echo (type(1) == type)\n"
        "echo type(4.4)(2)\n"
        "echo type(1)(2.9)\n"
        "echo type(true)(0)\n"
        "echo type(\"x\")(5)\n"
        "echo type([1])((2,3))\n"
        "echo type((1,2))([3,4])\n"
        "echo bool(0)\n"
        "echo bool(1)\n"
        "echo bool(\"\")\n"
        "echo bool(\"x\")\n"
        "echo bool([])\n"
        "echo bool([1])\n"
        "echo list(\"ab\")\n"
        "echo list((1,2,3))\n"
        "echo len(\"中文\")\n"
        "echo len([1,2])\n"
        "echo len((1,2,3))\n"
        "echo str(1)\n"
        "echo [int, len]\n"
        "echo [str, string]\n");
    expectOutput(
        result,
        "int\n"
        "double\n"
        "string\n"
        "bool\n"
        "list\n"
        "point\n"
        "callable\n"
        "callable\n"
        "callable\n"
        "true\n"
        "false\n"
        "2\n"
        "2\n"
        "false\n"
        "5\n"
        "[2, 3]\n"
        "(3, 4)\n"
        "false\n"
        "true\n"
        "false\n"
        "true\n"
        "false\n"
        "true\n"
        "[\"a\", \"b\"]\n"
        "[1, 2, 3]\n"
        "2\n"
        "2\n"
        "3\n"
        "1\n"
        "[int, len]\n"
        "[string, string]\n");
}

LUSH_TEST(eval_numeric) {
    Env env;
    const auto result = runScript(
        env,
        "echo 2/4\n"
        "echo 2/4.0\n"
        "echo -3/2\n"
        "echo 2*3+4\n"
        "echo int(2.9)\n"
        "echo int(-2.9)\n"
        "echo double(2)\n"
        "echo (1 == 1.0)\n"
        "echo (\"a\" + \"b\")\n"
        "echo (1 < 1.5)\n"
        "echo (true ? \"y\" : \"n\")\n"
        "echo (false && (1/0) > 0)\n"
        "echo (true || (1/0) > 0)\n"
        "echo (-9223372036854775808 % -1)\n"
        "echo 1e+20\n"
        "echo 1e-7\n");
    expectOutput(result,
                 "0\n0.5\n-1\n10\n2\n-2\n2\ntrue\nab\ntrue\ny\nfalse\ntrue\n"
                 "0\n1e+20\n1e-7\n");
}

LUSH_TEST(eval_numeric_errors) {
    {
        Env env;
        const auto result = runScript(env, "echo (9223372036854775807 + 1)\n");
        expectErrorCode(result, ErrorCode::R005);
    }
    {
        Env env;
        const auto result = runScript(env, "echo (1/0)\n");
        expectErrorCode(result, ErrorCode::R004);
    }
    {
        Env env;
        const auto result = runScript(env, "echo (1%0)\n");
        expectErrorCode(result, ErrorCode::R004);
    }
    {
        Env env;
        const auto result = runScript(env, "echo (1e308 * 10)\n");
        expectErrorCode(result, ErrorCode::R009);
    }
    {
        Env env;
        const auto result = runScript(env, "echo (true && 1)\n");
        expectErrorCode(result, ErrorCode::R002);
    }
}

LUSH_TEST(eval_int_conversion_edges) {
    {
        Env env;
        const auto result =
            runScript(env, "echo int(-9223372036854775808.0)\n");
        expectOutput(result, "-9223372036854775808\n");
    }
    {
        Env env;
        const auto result = runScript(env, "echo int(9223372036854775808.0)\n");
        expectErrorCode(result, ErrorCode::R005);
    }
    {
        Env env;
        const auto result = runScript(env, "echo int(1e300)\n");
        expectErrorCode(result, ErrorCode::R005);
    }
    {
        Env env;
        const auto result =
            runScript(env, "echo (-9223372036854775808 / -1)\n");
        expectErrorCode(result, ErrorCode::R005);
    }
    {
        Env env;
        const auto result =
            runScript(env, "echo (-(-9223372036854775808))\n");
        expectErrorCode(result, ErrorCode::R005);
    }
    {
        Env env;
        const auto result =
            runScript(env, "echo (-9223372036854775808 - 1)\n");
        expectErrorCode(result, ErrorCode::R005);
    }
    {
        Env env;
        const auto result = runScript(env, "echo bool((1,2))\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result = runScript(env, "echo list(1)\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result = runScript(env, "echo len(1)\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result = runScript(env, "echo int(true)\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result = runScript(env, "echo point(1)\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result = runScript(env, "echo point(1,2,3,4)\n");
        expectErrorCode(result, ErrorCode::R002);
    }
}

LUSH_TEST(eval_numeric_string_conversion) {
    Env env;
    const auto result = runScript(
        env,
        "echo int(\"42\")\n"
        "echo int(\" +42 \")\n"
        "echo int(\"-42\")\n"
        "echo int(\"-0\")\n"
        "echo int(\"9223372036854775807\")\n"
        "echo int(\"-9223372036854775808\")\n"
        "echo double(\"3\")\n"
        "echo double(\"3.5\")\n"
        "echo double(\"1e3\")\n"
        "echo double(\" -1.25 \")\n"
        "echo double(\"1e-3\")\n");
    expectOutput(result,
                 "42\n42\n-42\n0\n9223372036854775807\n"
                 "-9223372036854775808\n3\n3.5\n1000\n-1.25\n0.001\n");
}

LUSH_TEST(eval_numeric_string_errors) {
    const auto expectCode = [](std::string source, ErrorCode code) {
        Env env;
        const auto result = runScript(env, std::move(source));
        expectErrorCode(result, code);
    };

    expectCode("echo int(\"\")\n", ErrorCode::R013);
    expectCode("echo int(\"   \")\n", ErrorCode::R013);
    expectCode("echo int(\"1.0\")\n", ErrorCode::R013);
    expectCode("echo int(\"0x10\")\n", ErrorCode::R013);
    expectCode("echo int(\"1 2\")\n", ErrorCode::R013);
    expectCode("echo int(\"nan\")\n", ErrorCode::R013);

    expectCode("echo double(\"\")\n", ErrorCode::R013);
    expectCode("echo double(\"1.\")\n", ErrorCode::R013);
    expectCode("echo double(\".5\")\n", ErrorCode::R013);
    expectCode("echo double(\"1e\")\n", ErrorCode::R013);
    expectCode("echo double(\"1e+\")\n", ErrorCode::R013);
    expectCode("echo double(\"nan\")\n", ErrorCode::R013);
    expectCode("echo double(\"inf\")\n", ErrorCode::R013);
    expectCode("echo double(\"0x10\")\n", ErrorCode::R013);
    expectCode("echo double(\"1e309\")\n", ErrorCode::R013);
    expectCode("echo double(\"1e-400\")\n", ErrorCode::R013);

    expectCode("echo int(\"9223372036854775808\")\n", ErrorCode::R005);
    expectCode("echo int(\"-9223372036854775809\")\n", ErrorCode::R005);
    expectCode("echo int(\"999999999999999999999999\")\n",
               ErrorCode::R005);
}

LUSH_TEST(eval_string_conversion) {
    Env env;
    const auto result = runScript(
        env, "echo string([1,2])\necho string((1,2))\necho string(true)\n"
             "echo [string([1])]\n");
    expectOutput(result, "[1, 2]\n(1, 2)\ntrue\n[\"[1]\"]\n");
}

LUSH_TEST(eval_point_and_members) {
    {
        Env env;
        const auto result = runScript(
            env, "p = (1,2)\necho p.x\np.y = 9\necho p\necho p[0]\n");
        expectOutput(result, "1\n(1, 9)\n1\n");
    }
    {
        Env env;
        const auto result = runScript(env, "p = (1,2)\necho p.z\n");
        expectErrorCode(result, ErrorCode::R003);
    }
    {
        Env env;
        const auto result = runScript(env, "p = (1,2)\necho p[2]\n");
        expectErrorCode(result, ErrorCode::R003);
    }
    {
        Env env;
        const auto result = runScript(
            env, "a = [1,2]\necho a.length\na.length = 3\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result =
            runScript(env, "echo point(1,2)\necho point([3,4])\n");
        expectOutput(result, "(1, 2)\n(3, 4)\n");
    }
    {
        Env env;
        const auto result = runScript(env, "echo point([1])\n");
        expectErrorCode(result, ErrorCode::R006);
    }
    {
        Env env;
        const auto result = runScript(env, "echo point(\"x\")\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result = runScript(env, "echo (1,\"a\")\n");
        expectErrorCode(result, ErrorCode::R006);
    }
}

LUSH_TEST(eval_validation_errors) {
    {
        Env env;
        const auto result = runScript(env, "int = 1\n");
        expectError(result, ErrorCode::S003, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "callable = 1\n");
        expectError(result, ErrorCode::S003, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "x = int++\n");
        expectError(result, ErrorCode::S003, 1, 5);
    }
    {
        Env env;
        const auto result = runScript(env, "int[0] = 1\n");
        expectError(result, ErrorCode::S003, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "for int in [1] {\n}\n");
        expectError(result, ErrorCode::S003, 1, 5);
    }
    {
        Env env;
        const auto result = runScript(env, "break\n");
        expectError(result, ErrorCode::S005, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "continue\n");
        expectError(result, ErrorCode::S005, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "int 1\n");
        expectNoError(result);
        LUSH_CHECK_EQ(result.output, std::string());
    }
    {
        Env env;
        const auto result = runScript(env, "int(1)\n");
        expectNoError(result);
        LUSH_CHECK_EQ(result.output, std::string());
    }
    {
        Env env;
        const auto result = runScript(env, "x = echo(1)\n");
        expectError(result, ErrorCode::S008, 1, 5);
    }
    {
        Env env;
        const auto result = runScript(env, "int = ref x\n");
        expectError(result, ErrorCode::S003, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "x = echo\n");
        expectError(result, ErrorCode::S008, 1, 5);
    }
    {
        Env env;
        const auto result = runScript(env, "echo echo\n");
        expectError(result, ErrorCode::S008, 1, 6);
    }
    {
        Env env;
        const auto result = runScript(env, "foo 1\n");
        expectError(result, ErrorCode::S001, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "x = ref int\n");
        expectError(result, ErrorCode::R001, 1, 9);
    }
    {
        Env env;
        const auto result = runScript(env, "x = int[0]\n");
        expectError(result, ErrorCode::R002, 1, 9);
    }
    {
        Env env;
        const auto result = runScript(env, "x = *int\n");
        expectError(result, ErrorCode::R001, 1, 6);
    }
}

LUSH_TEST(eval_user_function_validation) {
    {
        Env env;
        const auto result = runScript(
            env,
            "def add(a: int, b: point2) -> double { return a }\n"
            "def empty() {}\n"
            "def value() -> int { return 1 }\n");
        expectNoError(result);
        LUSH_CHECK(env.functions().contains("ADD"));
        LUSH_CHECK(env.functions().contains("empty"));
        LUSH_CHECK(env.functions().contains("value"));
    }
    {
        Env env;
        const auto result =
            runScript(env, "def a() { b 1 }\ndef b(x) {}\n");
        expectNoError(result);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(a: int, A: int) {}\n");
        expectNoError(result);
    }
    {
        Env env;
        const auto result = runScript(env, "def f() -> void { return }\n");
        expectNoError(result);
    }
    {
        Env env;
        const auto result = runScript(env, "def f() { return 1 }\n");
        expectErrorCode(result, ErrorCode::S012);
    }
    {
        Env env;
        const auto result = runScript(env, "def f() -> int { return }\n");
        expectErrorCode(result, ErrorCode::S012);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(a) {}\nf 1 2\n");
        expectErrorCode(result, ErrorCode::S002);
    }
    {
        Env env;
        const auto result =
            runScript(env, "def f(a) -> int { return a }\nx = f()\n");
        expectErrorCode(result, ErrorCode::R002);
    }
    {
        Env env;
        const auto result = runScript(env, "def f() {}\nx = f()\n");
        expectErrorCode(result, ErrorCode::S012);
    }
    {
        Env env;
        const auto result = runScript(env, "def f() {}\nx = f\n");
        expectErrorCode(result, ErrorCode::S012);
    }
    {
        Env env;
        const auto result = runScript(env, "return 1\n");
        expectError(result, ErrorCode::S010, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "if (true) return 1\n");
        expectErrorCode(result, ErrorCode::S010);
    }
    {
        Env env;
        const auto result = runScript(env, "def f() { def g() {} }\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def Foo() { foo = 1 }\n");
        expectErrorCode(result, ErrorCode::S003);
    }
    {
        Env env;
        const auto result = runScript(env, "def Foo() {}\nfoo = 1\n");
        expectErrorCode(result, ErrorCode::S003);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(a: unknown) {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(a: void) {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(a: int, a: int) {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(f: int) {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def int() {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def point2() {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(callable) {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def f() {}\ndef f() {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def Foo() {}\ndef foo() {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result =
            runScript(env, "def f() { return 1 }\ndef f() {}\n");
        expectErrorCode(result, ErrorCode::S012);
    }
    {
        Env env;
        const auto result = runScript(env, "{\ndef f() {}\n}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        const auto result = runScript(env, "def f(point2) {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        BuiltinCatalog catalog;
        const auto result = runScriptWithCatalog(
            env, "def f() {}\ndef f() {}\n", catalog,
            ValidationOptions{false, true});
        expectNoError(result);
    }
    {
        Env env;
        env.create("foo", Value::makeInt(1));
        const auto result = runScript(env, "def Foo() {}\n");
        expectErrorCode(result, ErrorCode::S003);
    }
    {
        Env env;
        FunctionSignature existing;
        existing.name = Name{"Foo", {}};
        env.functions().insert(existing);
        const auto result = runScript(env, "def foo() {}\n");
        expectErrorCode(result, ErrorCode::S011);
    }
    {
        Env env;
        BuiltinCatalog catalog;
        FunctionSignature existing;
        existing.name = Name{"Foo", {}};
        env.functions().insert(existing);
        const auto result = runScriptWithCatalog(
            env, "def foo() {}\n", catalog,
            ValidationOptions{false, true});
        expectNoError(result);
    }
    {
        Env env;
        const auto result = runScript(env, "def f()\nx = 1\n");
        expectNoError(result);
        LUSH_CHECK(!hasVariable(env, "x"));
    }
    {
        Env env;
        const auto result = runScript(env, "def f()\n;\n");
        expectNoError(result);
        LUSH_CHECK(env.functions().contains("f"));
    }
    {
        Env env;
        const auto result = runScript(env, "def f()\n\nx = 1\n");
        expectErrorCode(result, ErrorCode::P002);
    }
}

LUSH_TEST(eval_type_token) {
    Env env;
    const auto result = runScript(
        env,
        "echo callable\n"
        "echo type(callable)\n"
        "echo type(int)\n"
        "echo (type(int) == callable)\n"
        "echo (type(1) == callable)\n"
        "echo string(callable)\n"
        "echo [callable]\n");
    expectOutput(result,
                 "callable\ncallable\ncallable\ntrue\nfalse\ncallable\n"
                 "[callable]\n");
}

LUSH_TEST(eval_type_token_is_not_callable) {
    {
        Env env;
        const auto result = runScript(env, "echo callable(1)\n");
        expectErrorCode(result, ErrorCode::R012);
    }
    {
        Env env;
        const auto result = runScript(env, "x = callable\necho x(1)\n");
        expectErrorCode(result, ErrorCode::R012);
    }
    {
        Env env;
        const auto result = runScript(env, "echo bool(callable)\n");
        expectErrorCode(result, ErrorCode::R002);
    }
}

LUSH_TEST(eval_case_insensitive_names) {
    {
        Env env;
        const auto result = runScript(
            env, "ECHO 1\necho INT(1)\necho (type(INT) == callable)\n");
        expectOutput(result, "1\n1\ntrue\n");
    }
    {
        Env env;
        const auto result = runScript(env, "ECHO = 1\n");
        expectError(result, ErrorCode::S003, 1, 1);
    }
    {
        Env env;
        const auto result = runScript(env, "x = ECHO\n");
        expectError(result, ErrorCode::S008, 1, 5);
    }
}

LUSH_TEST(eval_loops_break_and_empty_statement) {
    Env env;
    const auto result = runScript(
        env, "if (true);\necho 1\nfor (;;) {\n    break\n}\necho 2\n");
    expectOutput(result, "1\n2\n");
}

LUSH_TEST(eval_for_in_continue) {
    Env env;
    const auto result = runScript(
        env, "for x in [1,2] {\n    if (x == 1) continue\n    echo x\n}\n");
    expectOutput(result, "2\n");
}

LUSH_TEST(eval_string_index_and_length) {
    Env env;
    const auto result = runScript(
        env, "s = \"中文\"\necho s[0]\necho s[1]\necho len(s)\n");
    expectOutput(result, "中\n文\n2\n");
}

} // namespace

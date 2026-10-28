#include "test_harness.h"

#include "eval_test_utils.h"

#include "lush/core/cell.h"
#include "lush/core/value.h"

#include <cstdint>
#include <memory>
#include <string>

namespace {

using lush::core::Cell;
using lush::core::Env;
using lush::core::ErrorCode;
using lush::core::Value;
using lush::test::expectError;
using lush::test::expectOutput;
using lush::test::hasVariable;
using lush::test::intValue;
using lush::test::runScript;

LUSH_TEST(ref_cell_basics) {
    auto target = Cell::makeValue(Value::makeInt(1));
    auto reference = Cell::makeRef(target);

    LUSH_CHECK(!target->hasInboundRef());
    target->markInboundRef();
    LUSH_CHECK(target->hasInboundRef());

    LUSH_CHECK(reference->isRef());
    LUSH_CHECK_EQ(reference->read().asInt(), 1);
    target->rebind(Value::makeInt(2));
    LUSH_CHECK_EQ(reference->read().asInt(), 2);
    LUSH_CHECK(reference->effective() == target);

    reference->bindRef(Cell::makeValue(Value::makeInt(7)));
    LUSH_CHECK_EQ(reference->read().asInt(), 7);
}

LUSH_TEST(ref_g05_element_and_rebind) {
    Env env;
    const auto result = runScript(
        env, "a = [1,2]\nh = ref a\nh[0] = 7\necho a\nh = 100\necho a\n"
             "echo h\n");
    expectOutput(result, "[7, 2]\n[7, 2]\n100\n");
    LUSH_CHECK_EQ(intValue(env, "h"), 100);
}

LUSH_TEST(ref_g06_whole_value_write_through) {
    Env env;
    const auto result = runScript(
        env, "a = [1,2]\nh = ref a\n*h = [5,6]\necho a\n");
    expectOutput(result, "[5, 6]\n");
}

LUSH_TEST(ref_g07_loop_variable_lifetime) {
    Env env;
    const auto result = runScript(
        env, "r = 0\nfor i in [10,20] {\n    r = ref i\n}\necho *r\n");
    expectOutput(result, "20\n");
}

LUSH_TEST(ref_g35_ref_of_ref) {
    Env env;
    const auto result =
        runScript(env, "x = 1\ny = ref x\nz = ref y\n");
    expectError(result, ErrorCode::R008, 3, 9);
}

LUSH_TEST(ref_g36_deref_non_ref) {
    Env env;
    const auto result = runScript(env, "x = 1\n*x = 2\n");
    expectError(result, ErrorCode::R007, 2, 1);
}

LUSH_TEST(ref_g50_inbound_cannot_rebind_to_ref) {
    Env env;
    const auto result =
        runScript(env, "x = 1\nz = 2\ny = ref x\nx = ref z\n");
    expectError(result, ErrorCode::R008, 4, 1);
}

LUSH_TEST(ref_g51_inbound_can_still_be_assigned) {
    Env env;
    const auto result =
        runScript(env, "x = 1\ny = ref x\nx = 5\necho y\n");
    expectOutput(result, "5\n");
}

LUSH_TEST(ref_g52_self_reference) {
    Env env;
    const auto result = runScript(env, "h = 1\nh = ref h\n");
    expectError(result, ErrorCode::R008, 2, 9);
}

LUSH_TEST(ref_inbound_flag_survives_scope) {
    Env env;
    const auto result = runScript(
        env, "x = 1\nz = 2\n{\n    y = ref x\n}\nx = ref z\n");
    expectError(result, ErrorCode::R008, 6, 1);
    LUSH_CHECK(!hasVariable(env, "y"));
}

LUSH_TEST(ref_multiple_references_to_same_cell) {
    Env env;
    const auto result = runScript(
        env, "x = 1\ny = ref x\nz = ref x\n*y = 5\necho z\n");
    expectOutput(result, "5\n");
}

LUSH_TEST(ref_c_for_same_cell) {
    Env env;
    const auto result = runScript(
        env, "r = 0\nfor (i = 0; i < 3; i++) {\n    r = ref i\n}\necho *r\n");
    expectOutput(result, "3\n");
}

LUSH_TEST(ref_increment_through_ref) {
    Env env;
    const auto result = runScript(
        env, "x = 1\nh = ref x\ny = h++\necho x\necho h\necho y\n");
    expectOutput(result, "2\n2\n1\n");
}

LUSH_TEST(ref_rebind_while_holding_ref) {
    Env env;
    const auto result = runScript(
        env, "x = 1\ny = 2\nh = ref x\nh = ref y\necho *h\n");
    expectOutput(result, "2\n");
}

LUSH_TEST(ref_normal_assignment_breaks_alias) {
    Env env;
    const auto result = runScript(
        env, "x = 1\nh = ref x\nh = h\necho x\necho h\n");
    expectOutput(result, "1\n1\n");
}

LUSH_TEST(ref_to_callable) {
    Env env;
    const auto result = runScript(
        env, "f = int\nh = ref f\necho h\necho type(h)\nf = double\n"
             "echo h\n");
    expectOutput(result, "int\ncallable\ndouble\n");
}

LUSH_TEST(ref_write_through_point) {
    Env env;
    const auto result = runScript(
        env, "p = (1,2)\nh = ref p\nh.x = 3\necho p\nh[0] = 9\necho p\n"
             "*h = (4,5)\necho p\n");
    expectOutput(result, "(3, 2)\n(9, 2)\n(4, 5)\n");
}

LUSH_TEST(ref_string_element_is_read_only) {
    Env env;
    const auto result = runScript(
        env, "s = \"ab\"\nh = ref s\nh[0] = \"x\"\n");
    expectError(result, ErrorCode::R002, 3, 3);
}

} // namespace

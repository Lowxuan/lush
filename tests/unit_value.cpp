#include "test_harness.h"

#include "lush/core/builtins.h"
#include "lush/core/format.h"
#include "lush/core/value.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using lush::core::BuiltinCatalog;
using lush::core::ListValue;
using lush::core::PointValue;
using lush::core::Value;
using lush::core::ValueType;
using lush::core::formatDouble;
using lush::core::formatValue;
using lush::core::formatValueNested;
using lush::core::valueTypeName;

LUSH_TEST(value_basic_types) {
    const Value integer = Value::makeInt(42);
    const Value real = Value::makeDouble(0.5);
    const Value boolean = Value::makeBool(true);
    const Value text = Value::makeString("hello");

    LUSH_CHECK(integer.isInt());
    LUSH_CHECK(integer.type() == ValueType::Int);
    LUSH_CHECK_EQ(std::string(valueTypeName(integer.type())), std::string("int"));
    LUSH_CHECK(real.type() == ValueType::Double);
    LUSH_CHECK(boolean.type() == ValueType::Bool);
    LUSH_CHECK(text.type() == ValueType::String);
    LUSH_CHECK_EQ(text.asString(), "hello");
}

LUSH_TEST(value_list_deep_copy) {
    ListValue inner;
    inner.items().push_back(Value::makeInt(2));

    ListValue outer;
    outer.items().push_back(Value::makeInt(1));
    outer.items().push_back(Value::makeList(std::move(inner)));

    Value original = Value::makeList(std::move(outer));
    Value copy = original;
    copy.asList().items()[1].asList().items()[0] = Value::makeInt(9);

    LUSH_CHECK_EQ(original.asList().items()[1].asList().items()[0].asInt(), 2);
    LUSH_CHECK_EQ(copy.asList().items()[1].asList().items()[0].asInt(), 9);
}

LUSH_TEST(value_point_copy) {
    PointValue point;
    point.dim = 2;
    point[0] = 1.0;
    point[1] = 2.0;

    Value original = Value::makePoint(point);
    Value copy = original;
    copy.asPoint()[1] = 9.0;

    LUSH_CHECK_EQ(original.asPoint()[1], 2.0);
    LUSH_CHECK_EQ(copy.asPoint()[1], 9.0);
}

LUSH_TEST(value_callable_identity) {
    BuiltinCatalog catalog;
    const auto intCallable = catalog.findCallable("int");
    const auto upperInt = catalog.findCallable("INT");
    const auto strCallable = catalog.findCallable("str");
    const auto stringCallable = catalog.findCallable("string");

    LUSH_CHECK(intCallable != nullptr);
    LUSH_CHECK(intCallable == upperInt);
    LUSH_CHECK(strCallable == stringCallable);
    LUSH_CHECK(catalog.findCallable("echo") == nullptr);
    LUSH_CHECK(catalog.isCommand("echo"));
    LUSH_CHECK(catalog.isCommand("ECHO"));
    LUSH_CHECK(catalog.isReserved("int"));
    LUSH_CHECK(catalog.isReserved("str"));
    LUSH_CHECK(catalog.isReserved("echo"));

    const Value value = Value::makeCallable(intCallable);
    LUSH_CHECK(value.isCallable());
    LUSH_CHECK(value.type() == ValueType::Callable);
    LUSH_CHECK_EQ(formatValue(value), "int");
    LUSH_CHECK(value == Value::makeCallable(intCallable));
    LUSH_CHECK(value != Value::makeCallable(catalog.findCallable("len")));
}

LUSH_TEST(value_type_token) {
    const Value token = Value::makeTypeToken();
    LUSH_CHECK(token.isTypeToken());
    LUSH_CHECK(!token.isCallable());
    LUSH_CHECK(token.type() == ValueType::TypeToken);
    LUSH_CHECK_EQ(std::string(valueTypeName(token.type())),
                  std::string("callable"));
    LUSH_CHECK_EQ(formatValue(token), "callable");
    LUSH_CHECK_EQ(formatValueNested(token), "callable");
    LUSH_CHECK(token == Value::makeTypeToken());
}

LUSH_TEST(value_equality) {
    LUSH_CHECK(Value::makeInt(1) == Value::makeDouble(1.0));
    LUSH_CHECK(Value::makeInt(1) != Value::makeDouble(1.5));
    LUSH_CHECK(Value::makeString("a") == Value::makeString("a"));
    LUSH_CHECK(Value::makeBool(true) == Value::makeBool(true));
    LUSH_CHECK(Value::makeInt(1) != Value::makeBool(true));

    ListValue left;
    left.items().push_back(Value::makeInt(1));
    ListValue right;
    right.items().push_back(Value::makeDouble(1.0));
    LUSH_CHECK(Value::makeList(std::move(left)) ==
               Value::makeList(std::move(right)));
}

LUSH_TEST(value_formatting) {
    LUSH_CHECK_EQ(formatDouble(0.0), "0");
    LUSH_CHECK_EQ(formatDouble(-0.0), "0");
    LUSH_CHECK_EQ(formatDouble(1.0), "1");
    LUSH_CHECK_EQ(formatDouble(0.5), "0.5");
    LUSH_CHECK_EQ(formatDouble(1e20), "1e+20");
    LUSH_CHECK_EQ(formatDouble(1e-7), "1e-7");

    LUSH_CHECK_EQ(formatValue(Value::makeString("a\nb")), "a\nb");
    LUSH_CHECK_EQ(formatValueNested(Value::makeString("a\nb")),
                  "\"a\\nb\"");
    LUSH_CHECK_EQ(formatValueNested(Value::makeString("a\tb")),
                  "\"a\\tb\"");
    LUSH_CHECK_EQ(formatValueNested(Value::makeString("a\\b")),
                  "\"a\\\\b\"");
    LUSH_CHECK_EQ(formatValueNested(Value::makeString("a\"b")),
                  "\"a\\\"b\"");
    LUSH_CHECK_EQ(formatValueNested(Value::makeString(std::string("\0", 1))),
                  "\"\\0\"");
    LUSH_CHECK_EQ(formatValueNested(Value::makeString(std::string("\x01", 1))),
                  "\"\\u0001\"");
    LUSH_CHECK_EQ(formatValueNested(Value::makeString(std::string("\x7F", 1))),
                  "\"\\u007F\"");

    ListValue list;
    list.items().push_back(Value::makeInt(1));
    list.items().push_back(Value::makeString("a"));
    LUSH_CHECK_EQ(formatValue(Value::makeList(std::move(list))),
                  "[1, \"a\"]");

    PointValue point;
    point.dim = 3;
    point[0] = 1.0;
    point[1] = 2.5;
    point[2] = -3.0;
    LUSH_CHECK_EQ(formatValue(Value::makePoint(point)), "(1, 2.5, -3)");
}

} // namespace

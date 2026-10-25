#include "test_harness.h"

#include "eval_test_utils.h"

#include "lush/command/registry.h"
#include "lush/command/spec.h"
#include "lush/core/cell.h"
#include "lush/driver/driver.h"
#include "reference/reference_host.h"

#include <sstream>
#include <string>
#include <vector>

namespace {

using lush::command::CommandResult;
using lush::command::CommandSpec;
using lush::command::ParamSpec;
using lush::command::ParamType;
using lush::command::RegistrationError;
using lush::command::ReturnKind;
using lush::command::ReturnType;
using lush::core::Env;
using lush::core::ErrorCode;
using lush::core::Value;
using lush::core::ValueType;
using lush::driver::Driver;
using lush::driver::DriverResult;
using lush::driver::DriverStatus;
using lush::reference::ReferenceHost;
using lush::test::StringOutput;

DriverResult runReferenceScript(ReferenceHost& host, Env& env,
                                StringOutput& output, std::string text) {
    Driver driver(env, host.registry(), output);
    return driver.runScript(std::move(text));
}

void expectError(const DriverResult& result, ErrorCode code) {
    LUSH_CHECK(result.status == DriverStatus::Error);
    LUSH_CHECK(result.diagnostic.has_value());
    if (result.diagnostic) {
        LUSH_CHECK_EQ(static_cast<int>(result.diagnostic->code),
                      static_cast<int>(code));
    }
}

LUSH_TEST(reference_read_prompt_and_value) {
    std::istringstream input("hello\n");
    StringOutput output;
    ReferenceHost host(input, output);
    Env env;

    const DriverResult result =
        runReferenceScript(host, env, output, "echo read(\"提示: \")\n");
    LUSH_CHECK(result.status == DriverStatus::Done);
    LUSH_CHECK_EQ(output.text(), std::string("提示: hello\n"));
}

LUSH_TEST(reference_read_empty_line) {
    std::istringstream input("\n");
    StringOutput output;
    ReferenceHost host(input, output);
    Env env;

    const DriverResult result =
        runReferenceScript(host, env, output, "echo read()\n");
    LUSH_CHECK(result.status == DriverStatus::Done);
    LUSH_CHECK_EQ(output.text(), std::string("\n"));
}

LUSH_TEST(reference_read_crlf) {
    std::istringstream input("hello\r\n");
    StringOutput output;
    ReferenceHost host(input, output);
    Env env;

    const DriverResult result =
        runReferenceScript(host, env, output, "echo read()\n");
    LUSH_CHECK(result.status == DriverStatus::Done);
    LUSH_CHECK_EQ(output.text(), std::string("hello\n"));
}

LUSH_TEST(reference_read_eof_is_r010) {
    std::istringstream input;
    StringOutput output;
    ReferenceHost host(input, output);
    Env env;

    const DriverResult result =
        runReferenceScript(host, env, output, "echo read()\n");
    expectError(result, ErrorCode::R010);
    LUSH_CHECK(output.text().empty());
}

LUSH_TEST(reference_read_without_io_is_r010) {
    StringOutput output;
    ReferenceHost host;
    Env env;

    const DriverResult result =
        runReferenceScript(host, env, output, "echo read()\n");
    expectError(result, ErrorCode::R010);
}

LUSH_TEST(reference_read_invalid_utf8_is_r010) {
    std::string inputText;
    inputText.push_back(static_cast<char>(0xFF));
    inputText.push_back('\n');
    std::istringstream input(inputText);
    StringOutput output;
    ReferenceHost host(input, output);
    Env env;

    const DriverResult result =
        runReferenceScript(host, env, output, "echo read()\n");
    expectError(result, ErrorCode::R010);
}

LUSH_TEST(reference_read_to_numeric) {
    std::istringstream input(" 42 \n1.5\n");
    StringOutput output;
    ReferenceHost host(input, output);
    Env env;

    const DriverResult result = runReferenceScript(
        host, env, output, "echo int(read())\necho double(read())\n");
    LUSH_CHECK(result.status == DriverStatus::Done);
    LUSH_CHECK_EQ(output.text(), std::string("42\n1.5\n"));
}

LUSH_TEST(reference_read_invalid_numeric_is_r013) {
    std::istringstream input("abc\n");
    StringOutput output;
    ReferenceHost host(input, output);
    Env env;

    const DriverResult result =
        runReferenceScript(host, env, output, "echo int(read())\n");
    expectError(result, ErrorCode::R013);
}

LUSH_TEST(reference_read_return_type_metadata) {
    ReferenceHost host;
    const auto* signature = host.registry().findSignature("read");
    LUSH_CHECK(signature != nullptr);
    if (signature) {
        LUSH_CHECK(signature->returnsValue);
        LUSH_CHECK(signature->returnType.has_value());
        if (signature->returnType) {
            LUSH_CHECK(signature->returnType->type == ValueType::String);
            LUSH_CHECK_EQ(signature->returnType->dim, 0);
        }
    }
}

LUSH_TEST(command_return_type_registration) {
    lush::command::Registry registry;

    CommandSpec function;
    function.name = "value_fn";
    function.returns = ReturnKind::Value;
    function.returnType = ReturnType{ValueType::Point, 2};
    const auto functionResult = registry.registerFunction(
        std::move(function),
        [](const std::vector<Value>&,
           const lush::core::CommandContext&) {
            return CommandResult::ok(Value::makeInt(0));
        });
    LUSH_CHECK(functionResult.ok());
    const auto* signature = registry.findSignature("value_fn");
    LUSH_CHECK(signature != nullptr);
    if (signature && signature->returnType) {
        LUSH_CHECK(signature->returnType->type == ValueType::Point);
        LUSH_CHECK_EQ(signature->returnType->dim, 2);
    }

    CommandSpec badVoid;
    badVoid.name = "bad_void";
    badVoid.returnType = ReturnType{ValueType::Int, 0};
    const auto badVoidResult =
        registry.registerCommand(std::move(badVoid),
                                 [](const std::vector<Value>&,
                                    const lush::core::CommandContext&,
                                    lush::core::Output&) {
                                     return CommandResult::done();
                                 });
    LUSH_CHECK(!badVoidResult.ok());
    if (badVoidResult.error) {
        LUSH_CHECK(*badVoidResult.error ==
                   RegistrationError::ReturnTypeMismatch);
    }

    CommandSpec badDim;
    badDim.name = "bad_dim";
    badDim.returns = ReturnKind::Value;
    badDim.returnType = ReturnType{ValueType::Point, 1};
    const auto badDimResult = registry.registerFunction(
        std::move(badDim),
        [](const std::vector<Value>&,
           const lush::core::CommandContext&) {
            return CommandResult::ok(Value::makeInt(0));
        });
    LUSH_CHECK(!badDimResult.ok());
    if (badDimResult.error) {
        LUSH_CHECK(*badDimResult.error == RegistrationError::InvalidReturnDim);
    }
}

} // namespace

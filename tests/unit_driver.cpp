#include "test_harness.h"

#include "eval_test_utils.h"

#include "lush/command/host.h"
#include "lush/command/registry.h"
#include "lush/command/spec.h"
#include "lush/core/format.h"
#include "lush/core/value.h"
#include "lush/driver/driver.h"
#include "reference/reference_host.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using lush::command::CommandResult;
using lush::command::CommandSpec;
using lush::command::ParamSpec;
using lush::command::ParamType;
using lush::command::Registry;
using lush::core::Env;
using lush::core::ErrorCode;
using lush::core::PointValue;
using lush::core::SourceId;
using lush::core::Value;
using lush::driver::CancelReason;
using lush::driver::Driver;
using lush::driver::DriverResult;
using lush::driver::DriverStatus;
using lush::driver::InputOrigin;
using lush::reference::ReferenceHost;
using lush::test::StringOutput;

struct ReferenceFixture {
    Env env;
    ReferenceHost host;
    StringOutput output;
    Driver driver{env, host.registry(), output};
};

ParamSpec makeParam(std::string name, ParamType type) {
    ParamSpec param;
    param.name = std::move(name);
    param.type = type;
    return param;
}

ParamSpec makeOptionalParam(std::string name, ParamType type, Value value) {
    ParamSpec param = makeParam(std::move(name), type);
    param.optional = true;
    param.defaultValue = std::move(value);
    return param;
}

bool isStatus(const DriverResult& result, DriverStatus status) {
    return result.status == status;
}

void checkError(const DriverResult& result, ErrorCode code) {
    LUSH_CHECK(result.status == DriverStatus::Error);
    LUSH_CHECK(result.diagnostic.has_value());
    if (result.diagnostic) {
        LUSH_CHECK_EQ(static_cast<int>(result.diagnostic->code),
                      static_cast<int>(code));
    }
}

PointValue point2(double x, double y) {
    PointValue point;
    point.dim = 2;
    point.coordinates[0] = x;
    point.coordinates[1] = y;
    return point;
}

PointValue point3(double x, double y, double z) {
    PointValue point;
    point.dim = 3;
    point.coordinates[0] = x;
    point.coordinates[1] = y;
    point.coordinates[2] = z;
    return point;
}

LUSH_TEST(driver_g22_line_prompts) {
    ReferenceFixture fixture;
    DriverResult result = fixture.driver.feedLine("line");
    LUSH_CHECK(isStatus(result, DriverStatus::NeedInput));
    LUSH_CHECK(result.input.has_value());
    if (result.input) {
        LUSH_CHECK_EQ(result.input->prompt, std::string("指定第一个点"));
        LUSH_CHECK_EQ(result.input->index, std::size_t{0});
    }

    result = fixture.driver.feedLine("0,0");
    LUSH_CHECK(isStatus(result, DriverStatus::NeedInput));
    LUSH_CHECK(result.input.has_value());
    if (result.input) {
        LUSH_CHECK_EQ(result.input->prompt, std::string("指定第二个点"));
        LUSH_CHECK_EQ(result.input->index, std::size_t{1});
    }

    result = fixture.driver.feedLine("1,1");
    LUSH_CHECK(isStatus(result, DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(), std::string("LINE (0, 0) (1, 1)\n"));
}

LUSH_TEST(driver_g22_line_partial) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line 0,0"),
                        DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("1,1"), DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(), std::string("LINE (0, 0) (1, 1)\n"));
}

LUSH_TEST(driver_session_accepts_list_as_point) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line"),
                        DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("[0,0]"),
                        DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("1,1"), DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(), std::string("LINE (0, 0) (1, 1)\n"));
}

LUSH_TEST(driver_script_executes_reference_host) {
    ReferenceFixture fixture;
    const DriverResult result = fixture.driver.runScript(
        "line 0,0 1,1\ncircle 5,5 2\n");
    LUSH_CHECK(isStatus(result, DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(),
                  std::string("LINE (0, 0) (1, 1)\nCIRCLE (5, 5) 2\n"));
}

LUSH_TEST(driver_script_missing_interactive_is_s006) {
    ReferenceFixture fixture;
    const DriverResult result = fixture.driver.runScript("line 0,0\n");
    checkError(result, ErrorCode::S006);
    LUSH_CHECK(result.source != nullptr);
    LUSH_CHECK_EQ(fixture.output.text(), std::string());
}

LUSH_TEST(driver_script_missing_noninteractive_is_s002) {
    ReferenceFixture fixture;
    const DriverResult result = fixture.driver.runScript("move 1\n");
    checkError(result, ErrorCode::S002);
}

LUSH_TEST(driver_multiline_block) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("if (true) {"),
                        DriverStatus::NeedMore));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("echo 2"),
                        DriverStatus::NeedMore));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("}"), DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(), std::string("2\n"));
}

LUSH_TEST(driver_pending_blank_cancels) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("if (true)"),
                        DriverStatus::NeedMore));
    const DriverResult cancelled = fixture.driver.feedLine("");
    LUSH_CHECK(isStatus(cancelled, DriverStatus::Cancelled));
    LUSH_CHECK_EQ(static_cast<int>(cancelled.cancelReason),
                  static_cast<int>(CancelReason::PendingInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("echo 1"), DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(), std::string("1\n"));
}

LUSH_TEST(driver_unclosed_blank_waits) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("if (true) {"),
                        DriverStatus::NeedMore));
    LUSH_CHECK(isStatus(fixture.driver.feedLine(""), DriverStatus::NeedMore));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("}"), DriverStatus::Done));
}

LUSH_TEST(driver_pending_comment_waits) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("if (true)"),
                        DriverStatus::NeedMore));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("# comment"),
                        DriverStatus::NeedMore));
    LUSH_CHECK(isStatus(fixture.driver.feedLine(";"), DriverStatus::Done));
}

LUSH_TEST(driver_session_comment_and_required_empty) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line"),
                        DriverStatus::NeedInput));
    const DriverResult comment = fixture.driver.feedLine("# comment");
    LUSH_CHECK(isStatus(comment, DriverStatus::NeedInput));
    LUSH_CHECK(comment.input.has_value());
    if (comment.input) {
        LUSH_CHECK_EQ(comment.input->prompt, std::string("指定第一个点"));
    }

    const DriverResult cancelled = fixture.driver.feedLine("");
    LUSH_CHECK(isStatus(cancelled, DriverStatus::Cancelled));
    LUSH_CHECK_EQ(static_cast<int>(cancelled.cancelReason),
                  static_cast<int>(CancelReason::EmptyRequired));
    LUSH_CHECK(cancelled.diagnostic.has_value());
    if (cancelled.diagnostic) {
        LUSH_CHECK_EQ(static_cast<int>(cancelled.diagnostic->code),
                      static_cast<int>(ErrorCode::R011));
    }
    LUSH_CHECK(cancelled.source != nullptr);
}

LUSH_TEST(driver_session_error_stays_current) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line"),
                        DriverStatus::NeedInput));

    const DriverResult error = fixture.driver.feedLine("x");
    LUSH_CHECK(isStatus(error, DriverStatus::Error));
    LUSH_CHECK(error.sessionActive);
    LUSH_CHECK(error.input.has_value());
    if (error.input) {
        LUSH_CHECK_EQ(error.input->prompt, std::string("指定第一个点"));
    }
    LUSH_CHECK(error.source != nullptr);

    LUSH_CHECK(isStatus(fixture.driver.feedLine("0,0"),
                        DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("1,1"),
                        DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(), std::string("LINE (0, 0) (1, 1)\n"));
}

LUSH_TEST(driver_session_eof_cancels) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line"),
                        DriverStatus::NeedInput));
    const DriverResult result = fixture.driver.endInput();
    LUSH_CHECK(isStatus(result, DriverStatus::Cancelled));
    LUSH_CHECK_EQ(static_cast<int>(result.cancelReason),
                  static_cast<int>(CancelReason::Session));
    LUSH_CHECK(!result.diagnostic.has_value());
}

LUSH_TEST(driver_pending_eof_is_error) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("if (true)"),
                        DriverStatus::NeedMore));
    const DriverResult result = fixture.driver.endInput();
    checkError(result, ErrorCode::P002);
    LUSH_CHECK(result.source != nullptr);
}

LUSH_TEST(driver_leading_empty_is_not_session) {
    ReferenceFixture fixture;
    const DriverResult result = fixture.driver.feedLine(";line 0,0");
    checkError(result, ErrorCode::S006);
}

LUSH_TEST(driver_trailing_empty_allows_session) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line;"),
                        DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("0,0"),
                        DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("1,1"),
                        DriverStatus::Done));
}

LUSH_TEST(driver_session_expression_with_spaces) {
    Registry registry;
    Env env;
    StringOutput output;
    Driver driver(env, registry, output);

    CommandSpec spec;
    spec.name = "num";
    spec.interactive = true;
    spec.params.push_back(makeParam("x", ParamType::Number));
    const auto registered = registry.registerCommand(
        std::move(spec),
        [](const std::vector<Value>& args,
           const lush::core::CommandContext&, lush::core::Output& out) {
            out.write(lush::core::formatValue(args[0]) + "\n");
            return CommandResult::done();
        });
    LUSH_CHECK(registered.ok());

    LUSH_CHECK(isStatus(driver.feedLine("num"), DriverStatus::NeedInput));
    const DriverResult done = driver.feedLine("1 + 2");
    LUSH_CHECK(isStatus(done, DriverStatus::Done));
    LUSH_CHECK_EQ(output.text(), std::string("3\n"));
}

LUSH_TEST(driver_optional_default_is_not_prompted) {
    Registry registry;
    Env env;
    StringOutput output;
    Driver driver(env, registry, output);

    CommandSpec spec;
    spec.name = "paint";
    spec.interactive = true;
    spec.params.push_back(makeParam("x", ParamType::Int));
    spec.params.push_back(
        makeOptionalParam("y", ParamType::Int, Value::makeInt(7)));
    const auto registered = registry.registerCommand(
        std::move(spec),
        [](const std::vector<Value>& args,
           const lush::core::CommandContext&, lush::core::Output& out) {
            out.write(lush::core::formatValue(args[0]) + " " +
                      lush::core::formatValue(args[1]) + "\n");
            return CommandResult::done();
        });
    LUSH_CHECK(registered.ok());

    LUSH_CHECK(isStatus(driver.feedLine("paint"), DriverStatus::NeedInput));
    const DriverResult done = driver.feedLine("5");
    LUSH_CHECK(isStatus(done, DriverStatus::Done));
    LUSH_CHECK_EQ(output.text(), std::string("5 7\n"));
}

LUSH_TEST(driver_source_ids_are_mixed_for_session) {
    Registry registry;
    Env env;
    StringOutput output;
    Driver driver(env, registry, output);
    auto sources = std::make_shared<std::vector<SourceId>>();
    auto nameSource = std::make_shared<SourceId>(0);

    CommandSpec spec;
    spec.name = "record";
    spec.interactive = true;
    spec.params.push_back(makeParam("a", ParamType::Int));
    spec.params.push_back(makeParam("b", ParamType::Int));
    const auto registered = registry.registerCommand(
        std::move(spec),
        [sources, nameSource](const std::vector<Value>&,
                              const lush::core::CommandContext& context,
                              lush::core::Output&) {
            *sources = context.argSources;
            *nameSource = context.nameSource;
            return CommandResult::done();
        });
    LUSH_CHECK(registered.ok());

    LUSH_CHECK(isStatus(driver.feedLine("record 1"), DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(driver.feedLine("2"), DriverStatus::Done));
    LUSH_CHECK_EQ(sources->size(), std::size_t{2});
    LUSH_CHECK(*nameSource != 0);
    if (sources->size() == 2) {
        LUSH_CHECK((*sources)[0] != 0);
        LUSH_CHECK((*sources)[1] != 0);
        LUSH_CHECK((*sources)[0] != (*sources)[1]);
    }
}

LUSH_TEST(driver_host_error_uses_session_arg_source) {
    Registry registry;
    Env env;
    StringOutput output;
    Driver driver(env, registry, output);

    CommandSpec spec;
    spec.name = "bad";
    spec.interactive = true;
    spec.params.push_back(makeParam("a", ParamType::Int));
    spec.params.push_back(makeParam("b", ParamType::Int));
    const auto registered = registry.registerCommand(
        std::move(spec),
        [](const std::vector<Value>&,
           const lush::core::CommandContext& context,
           lush::core::Output&) {
            return CommandResult::failure(
                "bad", std::nullopt,
                context.argSources.size() > 1 ? context.argSources[1]
                                              : lush::core::kNoSource);
        });
    LUSH_CHECK(registered.ok());

    LUSH_CHECK(isStatus(driver.feedLine("bad 1"), DriverStatus::NeedInput));
    const DriverResult result = driver.feedLine("2");
    checkError(result, ErrorCode::R010);
    LUSH_CHECK(result.source != nullptr);
    if (result.source) {
        LUSH_CHECK(result.source->text().find("2") != std::string::npos);
    }
}

LUSH_TEST(driver_host_function_error_uses_call_source) {
    Registry registry;
    Env env;
    StringOutput output;
    Driver driver(env, registry, output);

    CommandSpec spec;
    spec.name = "badfn";
    spec.returns = lush::command::ReturnKind::Value;
    spec.params.push_back(makeParam("x", ParamType::Int));
    const auto registered = registry.registerFunction(
        std::move(spec),
        [](const std::vector<Value>&,
           const lush::core::CommandContext& context) {
            const SourceId source =
                context.argSources.empty() ? context.nameSource
                                           : context.argSources.front();
            return CommandResult::failure("badfn", std::nullopt, source);
        });
    LUSH_CHECK(registered.ok());

    const DriverResult result = driver.runScript("echo badfn(1)\n");
    checkError(result, ErrorCode::R010);
    LUSH_CHECK(result.source != nullptr);
    if (result.source) {
        LUSH_CHECK(result.source->text().find("badfn") !=
                   std::string::npos);
    }
}

LUSH_TEST(driver_invalid_host_source_falls_back) {
    Registry registry;
    Env env;
    StringOutput output;
    Driver driver(env, registry, output);

    CommandSpec spec;
    spec.name = "bad";
    spec.interactive = true;
    spec.params.push_back(makeParam("a", ParamType::Int));
    spec.params.push_back(makeParam("b", ParamType::Int));
    const auto registered = registry.registerCommand(
        std::move(spec),
        [](const std::vector<Value>&,
           const lush::core::CommandContext&,
           lush::core::Output&) {
            return CommandResult::failure("bad", std::nullopt, 999);
        });
    LUSH_CHECK(registered.ok());

    LUSH_CHECK(isStatus(driver.feedLine("bad 1"), DriverStatus::NeedInput));
    const DriverResult result = driver.feedLine("2");
    checkError(result, ErrorCode::R010);
    LUSH_CHECK(result.source != nullptr);
    if (result.source) {
        LUSH_CHECK(result.source->text().find("bad 1") !=
                   std::string::npos);
    }
}

LUSH_TEST(driver_direct_point_origin) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line"),
                        DriverStatus::NeedInput));

    const DriverResult error = fixture.driver.submitPoint(point3(1, 2, 3));
    LUSH_CHECK(isStatus(error, DriverStatus::Error));
    LUSH_CHECK(error.sessionActive);
    LUSH_CHECK(error.origin.has_value());
    if (error.origin) {
        LUSH_CHECK_EQ(static_cast<int>(*error.origin),
                      static_cast<int>(InputOrigin::DirectPoint));
    }
    LUSH_CHECK(!error.source);
    LUSH_CHECK(error.diagnostic.has_value());
    if (error.diagnostic) {
        LUSH_CHECK_EQ(static_cast<int>(error.diagnostic->code),
                      static_cast<int>(ErrorCode::R002));
        LUSH_CHECK(error.diagnostic->message.find("直接提交") !=
                   std::string::npos);
    }

    LUSH_CHECK(isStatus(fixture.driver.submitPoint(point2(1, 2)),
                        DriverStatus::NeedInput));
    LUSH_CHECK(isStatus(fixture.driver.submitPoint(point2(3, 4)),
                        DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(),
                  std::string("LINE (1, 2) (3, 4)\n"));
}

LUSH_TEST(driver_cancel_session) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("line"),
                        DriverStatus::NeedInput));
    const DriverResult cancelled = fixture.driver.cancel();
    LUSH_CHECK(isStatus(cancelled, DriverStatus::Cancelled));
    LUSH_CHECK_EQ(static_cast<int>(cancelled.cancelReason),
                  static_cast<int>(CancelReason::Session));
    LUSH_CHECK(!cancelled.diagnostic.has_value());
    LUSH_CHECK(isStatus(fixture.driver.feedLine("echo 1"), DriverStatus::Done));
}

LUSH_TEST(driver_cancel_pending) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine("if (true)"),
                        DriverStatus::NeedMore));
    const DriverResult cancelled = fixture.driver.cancel();
    LUSH_CHECK(isStatus(cancelled, DriverStatus::Cancelled));
    LUSH_CHECK_EQ(static_cast<int>(cancelled.cancelReason),
                  static_cast<int>(CancelReason::PendingInput));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("echo 1"), DriverStatus::Done));
}

LUSH_TEST(driver_empty_and_comment_lines) {
    ReferenceFixture fixture;
    LUSH_CHECK(isStatus(fixture.driver.feedLine(""), DriverStatus::Done));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("# comment"),
                        DriverStatus::Done));
    LUSH_CHECK(isStatus(fixture.driver.feedLine("echo 1"), DriverStatus::Done));
    LUSH_CHECK_EQ(fixture.output.text(), std::string("1\n"));
}

} // namespace

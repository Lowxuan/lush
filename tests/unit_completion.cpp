#include "test_harness.h"

#include "lush/command/host.h"
#include "lush/command/registry.h"
#include "lush/command/session.h"
#include "lush/command/spec.h"
#include "lush/completion/completion.h"
#include "lush/core/cell.h"
#include "lush/core/source.h"
#include "lush/core/value.h"
#include "reference/reference_host.h"

#include <cstddef>
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
using lush::command::ReturnKind;
using lush::command::ReturnType;
using lush::completion::CompletionContext;
using lush::completion::CompletionItem;
using lush::completion::CompletionResult;
using lush::core::Env;
using lush::core::PointValue;
using lush::core::Value;
using lush::core::ValueType;

CompletionResult runCompletionAt(
    std::string text, std::size_t cursor, Env& env, const Registry& registry,
    std::optional<lush::command::NeedInput> session = std::nullopt) {
    lush::core::Source source(std::move(text));
    lush::completion::CompletionRequest request{
        source, cursor, env, registry, std::move(session)};
    return lush::completion::complete(request);
}

CompletionResult runCompletion(std::string text, Env& env,
                              const Registry& registry,
                              std::optional<lush::command::NeedInput> session =
                                  std::nullopt) {
    const std::size_t cursor = text.size();
    return runCompletionAt(std::move(text), cursor, env, registry,
                           std::move(session));
}

bool hasLabel(const CompletionResult& result, std::string_view label) {
    for (const CompletionItem& item : result.items) {
        if (item.label == label) {
            return true;
        }
    }
    return false;
}

const CompletionItem* findItem(const CompletionResult& result,
                               std::string_view label) {
    for (const CompletionItem& item : result.items) {
        if (item.label == label) {
            return &item;
        }
    }
    return nullptr;
}

PointValue point2(double x, double y) {
    PointValue point;
    point.dim = 2;
    point.coordinates[0] = x;
    point.coordinates[1] = y;
    return point;
}

ParamSpec intParam(std::string name) {
    ParamSpec param;
    param.name = std::move(name);
    param.type = ParamType::Int;
    return param;
}

void registerIntFunction(Registry& registry, std::string name,
                         std::optional<ReturnType> returnType) {
    CommandSpec spec;
    spec.name = std::move(name);
    spec.returns = ReturnKind::Value;
    spec.returnType = returnType;
    spec.params.push_back(intParam("x"));
    const auto result = registry.registerFunction(
        std::move(spec),
        [](const std::vector<Value>&,
           const lush::core::CommandContext&) {
            return CommandResult::ok(Value::makeInt(0));
        });
    LUSH_CHECK(result.ok());
}

LUSH_TEST(completion_command_point_argument) {
    lush::reference::ReferenceHost host;
    Env env;
    env.create("p", Value::makePoint(point2(0, 0)));

    CompletionResult result = runCompletion("line ", env, host.registry());
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::CommandArgument));
    LUSH_CHECK(hasLabel(result, "p"));
    LUSH_CHECK(hasLabel(result, "("));
    LUSH_CHECK(!hasLabel(result, "0"));
    LUSH_CHECK_EQ(result.replaceSpan.start, std::size_t{5});
    LUSH_CHECK_EQ(result.replaceSpan.end, std::size_t{5});

    result = runCompletion("line 1,2 ", env, host.registry());
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::CommandArgument));
    LUSH_CHECK(hasLabel(result, "p"));
    LUSH_CHECK(hasLabel(result, "("));
    LUSH_CHECK(!hasLabel(result, "0"));
}

LUSH_TEST(completion_complete_point_has_no_candidates) {
    lush::reference::ReferenceHost host;
    Env env;
    const CompletionResult result =
        runCompletion("line 1,2", env, host.registry());
    LUSH_CHECK(result.items.empty());
}

LUSH_TEST(completion_statement_start_and_call_distinction) {
    lush::reference::ReferenceHost host;
    Env env;

    CompletionResult result = runCompletion("line", env, host.registry());
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::StatementStart));
    LUSH_CHECK(hasLabel(result, "line"));

    result = runCompletion("line(", env, host.registry());
    LUSH_CHECK(result.context != CompletionContext::CallArgument);
    LUSH_CHECK(result.context != CompletionContext::CommandArgument);
}

LUSH_TEST(completion_function_call_argument) {
    Registry registry;
    Env env;
    env.create("i", Value::makeInt(1));
    registerIntFunction(registry, "f", ReturnType{ValueType::Int, 0});

    const CompletionResult result =
        runCompletion("x = f(", env, registry);
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::CallArgument));
    LUSH_CHECK(hasLabel(result, "i"));
    LUSH_CHECK(hasLabel(result, "0"));
    const CompletionItem* item = findItem(result, "i");
    LUSH_CHECK(item != nullptr);
    if (item) {
        LUSH_CHECK(item->valueType.has_value());
        if (item->valueType) {
            LUSH_CHECK(*item->valueType == ValueType::Int);
        }
    }
}

LUSH_TEST(completion_point_member) {
    Registry registry;
    Env env;
    env.create("p", Value::makePoint(point2(0, 0)));
    env.create("l", Value::makeList(lush::core::ListValue{}));
    env.create("s", Value::makeString("abc"));

    CompletionResult result = runCompletion("p.", env, registry);
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::Member));
    LUSH_CHECK_EQ(result.items.size(), std::size_t{2});
    LUSH_CHECK(hasLabel(result, "x"));
    LUSH_CHECK(hasLabel(result, "y"));
    LUSH_CHECK(!hasLabel(result, "z"));

    result = runCompletion("l.", env, registry);
    LUSH_CHECK(hasLabel(result, "length"));
    LUSH_CHECK(!hasLabel(result, "x"));

    result = runCompletion("s.", env, registry);
    LUSH_CHECK(hasLabel(result, "length"));

    result = runCompletion("(1,2).", env, registry);
    LUSH_CHECK(hasLabel(result, "x"));
    LUSH_CHECK(hasLabel(result, "y"));
    LUSH_CHECK(!hasLabel(result, "z"));
}

LUSH_TEST(completion_member_from_return_type) {
    lush::reference::ReferenceHost host;
    Env env;

    CompletionResult result = runCompletion("x = read().", env, host.registry());
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::Member));
    LUSH_CHECK(hasLabel(result, "length"));

    Registry registry;
    CommandSpec spec;
    spec.name = "point_func";
    spec.returns = ReturnKind::Value;
    spec.returnType = ReturnType{ValueType::Point, 2};
    LUSH_CHECK(registry.registerFunction(
                   std::move(spec),
                   [](const std::vector<Value>&,
                      const lush::core::CommandContext&) {
                       return CommandResult::ok(
                           Value::makePoint(PointValue{}));
                   })
                   .ok());
    result = runCompletion("x = point_func().", env, registry);
    LUSH_CHECK(hasLabel(result, "x"));
    LUSH_CHECK(hasLabel(result, "y"));
    LUSH_CHECK(!hasLabel(result, "z"));
}

LUSH_TEST(completion_for_iterable) {
    Registry registry;
    Env env;
    lush::core::ListValue list;
    list.items().push_back(Value::makeInt(1));
    env.create("l", Value::makeList(std::move(list)));
    env.create("s", Value::makeString("abc"));
    env.create("i", Value::makeInt(1));

    const CompletionResult result =
        runCompletion("for i in ", env, registry);
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::ForIterable));
    LUSH_CHECK(hasLabel(result, "l"));
    LUSH_CHECK(hasLabel(result, "s"));
    LUSH_CHECK(!hasLabel(result, "i"));
}

LUSH_TEST(completion_replaces_whole_token_at_cursor) {
    lush::reference::ReferenceHost host;
    Env env;

    const CompletionResult result =
        runCompletionAt("liXne", 2, env, host.registry());
    const CompletionItem* line = findItem(result, "line");
    LUSH_CHECK(line != nullptr);
    if (line) {
        LUSH_CHECK_EQ(line->replaceSpan.start, std::size_t{0});
        LUSH_CHECK_EQ(line->replaceSpan.end, std::size_t{5});
    }
}

LUSH_TEST(completion_any_lists_callables) {
    lush::reference::ReferenceHost host;
    Env env;
    env.create("i", Value::makeInt(1));

    const CompletionResult result =
        runCompletion("echo ", env, host.registry());
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::CommandArgument));
    LUSH_CHECK(hasLabel(result, "i"));
    LUSH_CHECK(hasLabel(result, "int"));
    LUSH_CHECK(hasLabel(result, "read"));
    LUSH_CHECK(!hasLabel(result, "\"\""));
}

LUSH_TEST(completion_for_in_keyword) {
    Registry registry;
    Env env;

    const CompletionResult result = runCompletion("for i ", env, registry);
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::StatementStart));
    LUSH_CHECK(hasLabel(result, "in"));
}

LUSH_TEST(completion_no_next_parameter) {
    lush::reference::ReferenceHost host;
    Env env;
    env.create("p", Value::makePoint(point2(0, 0)));

    const CompletionResult result =
        runCompletion("line 1,2 3,3 ", env, host.registry());
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::CommandArgument));
    LUSH_CHECK(result.items.empty());
}

LUSH_TEST(completion_comments_and_strings_have_no_candidates) {
    Registry registry;
    Env env;

    LUSH_CHECK(runCompletion("line # comment", env, registry).items.empty());
    LUSH_CHECK(runCompletion("echo \"unterminated", env, registry).items.empty());
    LUSH_CHECK(runCompletion("# comment", env, registry).items.empty());
}

LUSH_TEST(completion_session_point) {
    Registry registry;
    Env env;
    env.create("p", Value::makePoint(point2(1, 2)));
    lush::command::NeedInput input;
    input.prompt = "指定点";
    input.type = ParamType::Point;
    input.dim = 2;
    input.index = 0;

    const CompletionResult result =
        runCompletion("", env, registry, input);
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::SessionArgument));
    LUSH_CHECK(hasLabel(result, "p"));
    LUSH_CHECK(hasLabel(result, "("));
    const CompletionItem* item = findItem(result, "p");
    LUSH_CHECK(item != nullptr);
    if (item) {
        LUSH_CHECK(item->detail.find("指定点") != std::string::npos);
    }
}

LUSH_TEST(completion_case_and_alias) {
    Registry registry;
    Env env;
    env.create("x", Value::makeInt(1));
    env.create("X", Value::makeInt(2));

    CompletionResult result = runCompletion("x", env, registry);
    LUSH_CHECK(hasLabel(result, "x"));
    LUSH_CHECK(!hasLabel(result, "X"));

    result = runCompletion("x = str", env, registry);
    LUSH_CHECK(hasLabel(result, "str"));
}

LUSH_TEST(completion_type_mismatch_is_last) {
    lush::reference::ReferenceHost host;
    Env env;
    env.create("p", Value::makePoint(point2(0, 0)));
    env.create("i", Value::makeInt(1));

    const CompletionResult result =
        runCompletion("line ", env, host.registry());
    const CompletionItem* point = findItem(result, "p");
    const CompletionItem* integer = findItem(result, "i");
    LUSH_CHECK(point != nullptr);
    LUSH_CHECK(integer != nullptr);
    if (point && integer) {
        LUSH_CHECK(!point->typeMismatch);
        LUSH_CHECK(integer->typeMismatch);
        LUSH_CHECK(point < integer);
    }
}

LUSH_TEST(completion_read_arguments) {
    lush::reference::ReferenceHost host;
    Env env;

    const CompletionResult result =
        runCompletion("x = read(", env, host.registry());
    LUSH_CHECK_EQ(static_cast<int>(result.context),
                  static_cast<int>(CompletionContext::CallArgument));
    LUSH_CHECK(hasLabel(result, "\"\""));
}

} // namespace

#include "test_harness.h"

#include "eval_test_utils.h"

#include "lush/command/host.h"
#include "lush/command/registry.h"
#include "lush/command/spec.h"
#include "lush/core/builtins.h"
#include "lush/core/lexer.h"
#include "lush/core/parser.h"
#include "lush/core/source.h"
#include "lush/core/validate.h"
#include "reference/reference_host.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using lush::command::CommandResult;
using lush::command::CommandSpec;
using lush::command::ParamSpec;
using lush::command::ParamType;
using lush::command::RegistrationError;
using lush::command::ReturnKind;
using lush::command::Registry;
using lush::core::BuiltinCatalog;
using lush::core::CommandContext;
using lush::core::Diagnostic;
using lush::core::Env;
using lush::core::ErrorCode;
using lush::core::Lexer;
using lush::core::ListValue;
using lush::core::Output;
using lush::core::ParseOptions;
using lush::core::Source;
using lush::core::ValidationOptions;
using lush::core::Value;
using lush::reference::ReferenceHost;
using lush::test::expectError;
using lush::test::expectErrorCode;
using lush::test::expectNoError;
using lush::test::expectOutput;
using lush::test::runScriptWithCatalog;
using lush::test::ScriptResult;

ParamSpec makeParam(std::string name, ParamType type) {
    ParamSpec param;
    param.name = std::move(name);
    param.type = type;
    return param;
}

ParamSpec makePointParam(std::string name) {
    ParamSpec param = makeParam(std::move(name), ParamType::Point);
    param.dim = 2;
    return param;
}

ParamSpec makeOptionalNumberParam(std::string name, Value defaultValue) {
    ParamSpec param = makeParam(std::move(name), ParamType::Number);
    param.optional = true;
    param.defaultValue = std::move(defaultValue);
    return param;
}

CommandSpec makeSpec(std::string name, ReturnKind returns = ReturnKind::Void) {
    CommandSpec spec;
    spec.name = std::move(name);
    spec.returns = returns;
    return spec;
}

CommandResult okHandler(const std::vector<Value>&, const CommandContext&,
                        Output&) {
    return CommandResult::done();
}

void checkDiagnostic(const ScriptResult& result, ErrorCode code,
                     std::size_t line, std::size_t column) {
    LUSH_CHECK(result.diagnostic.has_value());
    if (!result.diagnostic) {
        return;
    }
    LUSH_CHECK_EQ(static_cast<int>(result.diagnostic->code),
                  static_cast<int>(code));
    const lush::core::LineCol position =
        result.source.lineCol(result.diagnostic->span.start);
    LUSH_CHECK_EQ(position.line, line);
    LUSH_CHECK_EQ(position.column, column);
}

ScriptResult runWith(ReferenceHost& host, Env& env, std::string text) {
    return runScriptWithCatalog(env, std::move(text), host.registry());
}

ScriptResult runWith(Registry& registry, Env& env, std::string text) {
    return runScriptWithCatalog(env, std::move(text), registry);
}

ScriptResult runWithBuiltins(Env& env, std::string text) {
    BuiltinCatalog catalog;
    return runScriptWithCatalog(env, std::move(text), catalog);
}

LUSH_TEST(command_g11_list_to_point) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(
        host, env, "line [0,0] [1,1]\ncircle [5,5] 2\n");
    expectOutput(result, "LINE (0, 0) (1, 1)\nCIRCLE (5, 5) 2\n");
}

LUSH_TEST(command_g12_bare_point) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(
        host, env,
        "line 0,0 1,1\nline -1,-1 2,2\necho 1,2,3\n");
    expectOutput(result,
                 "LINE (0, 0) (1, 1)\n"
                 "LINE (-1, -1) (2, 2)\n"
                 "(1, 2, 3)\n");
}

LUSH_TEST(command_g13_variables) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(
        host, env, "f = (0,0)\ng = (1,1)\nline f g\n");
    expectOutput(result, "LINE (0, 0) (1, 1)\n");
}

LUSH_TEST(command_g14_move_negative) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(host, env, "move -1 -2\n");
    expectOutput(result, "MOVE -1 -2\n");
}

LUSH_TEST(command_g15_operator_space) {
    ReferenceHost host;
    Env env;
    const auto result =
        runWith(host, env, "a = 1\nb = 2\nmove a - b\n");
    expectError(result, ErrorCode::P004, 3, 8);
    LUSH_CHECK_EQ(result.output, std::string());
}

LUSH_TEST(command_g16_bare_point_space) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(host, env, "echo 1, 2\n");
    expectError(result, ErrorCode::P004, 1, 6);
    LUSH_CHECK_EQ(result.output, std::string());
}

LUSH_TEST(command_g20_while_point) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(
        host, env,
        "f = (3,3)\ng = (6,6)\n"
        "while (f[1] > 0) {\n"
        "    line f g\n"
        "    f[1] = f[1] - 1\n"
        "}\n");
    expectOutput(result,
                 "LINE (3, 3) (6, 6)\n"
                 "LINE (3, 2) (6, 6)\n"
                 "LINE (3, 1) (6, 6)\n");
}

LUSH_TEST(command_g21_nested_for) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(
        host, env,
        "for i in [1,2] {\n"
        "    for j in [1,2] {\n"
        "        echo (i * 10 + j)\n"
        "    }\n"
        "}\n");
    expectOutput(result, "11\n12\n21\n22\n");
}

LUSH_TEST(command_g23_missing_interactive) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(host, env, "line 0,0\n");
    expectError(result, ErrorCode::S006, 1, 1);
    LUSH_CHECK_EQ(result.output, std::string());
}

LUSH_TEST(command_g24_unknown) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(host, env, "foo 1\n");
    expectError(result, ErrorCode::S001, 1, 1);
}

LUSH_TEST(command_g25_reserved) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(host, env, "line = 5\n");
    expectError(result, ErrorCode::S003, 1, 1);
}

LUSH_TEST(command_g37_nested_for_line) {
    ReferenceHost host;
    Env env;
    const auto result = runWith(
        host, env,
        "for i in [1,2] {\n"
        "    line (i,1) (1,i)\n"
        "}\n");
    expectOutput(result,
                 "LINE (1, 1) (1, 1)\n"
                 "LINE (2, 1) (1, 2)\n");
}

LUSH_TEST(command_missing_and_too_many_args) {
    ReferenceHost host;
    {
        Env env;
        const auto result = runWith(host, env, "move 1\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
    {
        Env env;
        const auto result = runWith(host, env, "line 0,0 1,1 2,2\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
    {
        Env env;
        const auto result = runWith(host, env, "move 1,2\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
}

LUSH_TEST(command_point_binding_errors) {
    ReferenceHost host;
    {
        Env env;
        const auto result = runWith(host, env, "line [1] 1,1\n");
        expectError(result, ErrorCode::R006, 1, 6);
    }
    {
        Env env;
        const auto result = runWith(host, env, "line \"x\" 1,1\n");
        expectError(result, ErrorCode::R002, 1, 6);
    }
    {
        Env env;
        const auto result = runWith(host, env, "line (1,2,3) 1,1\n");
        expectError(result, ErrorCode::R002, 1, 6);
    }
    {
        Env env;
        const auto result = runWith(host, env, "line [1,2,3] 1,1\n");
        expectError(result, ErrorCode::R002, 1, 6);
    }
}

LUSH_TEST(command_case_insensitive_and_alias) {
    ReferenceHost host;
    {
        Env env;
        const auto result = runWith(host, env, "LINE 0,0 1,1\n");
        expectOutput(result, "LINE (0, 0) (1, 1)\n");
    }

    Registry registry;
    CommandSpec spec = makeSpec("shift");
    spec.aliases.push_back("mv");
    const auto registered =
        registry.registerCommand(std::move(spec), okHandler);
    LUSH_CHECK(registered.ok());

    {
        Env env;
        const auto result = runWith(registry, env, "SHIFT\nMV\n");
        expectOutput(result, "");
    }
}

LUSH_TEST(command_signature_metadata) {
    ReferenceHost host;
    const auto* line = host.registry().findSignature("line");
    LUSH_CHECK(line != nullptr);
    if (!line) {
        return;
    }
    LUSH_CHECK_EQ(line->required, std::size_t{2});
    LUSH_CHECK_EQ(line->maxFixed, std::size_t{2});
    LUSH_CHECK(line->interactive);
    LUSH_CHECK(!line->returnsValue);
    LUSH_CHECK_EQ(line->params.size(), std::size_t{2});
    LUSH_CHECK_EQ(line->params[0].hint, std::string("指定第一个点"));
    LUSH_CHECK_EQ(line->params[1].hint, std::string("指定第二个点"));

    const auto* move = host.registry().findSignature("move");
    LUSH_CHECK(move != nullptr);
    if (move) {
        LUSH_CHECK(!move->interactive);
    }
}

LUSH_TEST(command_allow_interactive_session) {
    ReferenceHost host;
    Source source("line 0,0\n");
    Lexer lexer(source);
    const auto lex = lexer.tokenize();
    LUSH_CHECK(!lex.diagnostic.has_value());
    const auto parsed = lush::core::parse(lex, ParseOptions::Script);
    LUSH_CHECK(parsed.program != nullptr);
    if (!parsed.program) {
        return;
    }

    const auto allowed = lush::core::validate(
        *parsed.program, host.registry(), ValidationOptions{true});
    LUSH_CHECK(!allowed.diagnostic.has_value());

    const auto denied = lush::core::validate(
        *parsed.program, host.registry(), ValidationOptions{});
    LUSH_CHECK(denied.diagnostic.has_value());
    if (denied.diagnostic) {
        LUSH_CHECK_EQ(static_cast<int>(denied.diagnostic->code),
                      static_cast<int>(ErrorCode::S006));
    }
}

LUSH_TEST(command_builtin_statement_calls) {
    {
        Env env;
        const auto result = runWithBuiltins(env, "int 1\nint(1)\n");
        expectNoError(result);
        LUSH_CHECK_EQ(result.output, std::string());
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "int 1 2\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "int \"x\"\n");
        expectError(result, ErrorCode::R013, 1, 5);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "point 1 2 3 4\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "len\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo\n");
        expectOutput(result, "\n");
    }
}

LUSH_TEST(command_builtin_expression_calls) {
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo bool(\"yes\")\n");
        expectOutput(result, "true\n");
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo bool(0)\n");
        expectOutput(result, "false\n");
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo bool((1,2))\n");
        expectError(result, ErrorCode::R002, 1, 11);
    }
    {
        Env env;
        const auto result =
            runWithBuiltins(env, "echo bool(\"yes\",\"no\")\n");
        expectError(result, ErrorCode::R002, 1, 10);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo point(1,2)\n");
        expectOutput(result, "(1, 2)\n");
    }
    {
        Env env;
        const auto result =
            runWithBuiltins(env, "echo point(1,2,3,4)\n");
        expectError(result, ErrorCode::R002, 1, 11);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo point(1)\n");
        expectError(result, ErrorCode::R002, 1, 12);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo point([1])\n");
        expectError(result, ErrorCode::R006, 1, 12);
    }
    {
        Env env;
        const auto result = runWithBuiltins(env, "echo int(1,2)\n");
        expectError(result, ErrorCode::R002, 1, 9);
    }
}

LUSH_TEST(command_variable_callable) {
    ReferenceHost host;
    {
        Env env;
        const auto result = runWith(host, env, "t = int\nt 1\n");
        expectError(result, ErrorCode::S001, 2, 1);
    }
    {
        Env env;
        const auto result = runWith(host, env, "t = int\necho t(1)\n");
        expectOutput(result, "1\n");
    }
    {
        Env env;
        const auto result = runWith(host, env, "echo line\n");
        expectError(result, ErrorCode::S008, 1, 6);
    }
}

LUSH_TEST(command_host_value_function) {
    Registry registry;
    CommandSpec spec = makeSpec("twice", ReturnKind::Value);
    spec.params.push_back(makeParam("x", ParamType::Number));
    const auto registered = registry.registerFunction(
        std::move(spec), [](const std::vector<Value>& args,
                            const CommandContext&) {
            return CommandResult::ok(
                Value::makeDouble(args[0].asDouble() * 2.0));
        });
    LUSH_CHECK(registered.ok());

    {
        Env env;
        const auto result =
            runWith(registry, env, "echo twice(2)\ntwice 3\n");
        expectOutput(result, "4\n");
    }
    {
        Env env;
        const auto result = runWith(registry, env, "twice 1 2\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
    {
        Env env;
        const auto result = runWith(registry, env, "echo twice(1,2)\n");
        expectError(result, ErrorCode::R002, 1, 11);
    }
    {
        Env env;
        const auto result = runWith(registry, env, "echo twice(\"x\")\n");
        expectError(result, ErrorCode::R002, 1, 12);
    }
    {
        Env env;
        const auto result = runWith(registry, env, "twice = 1\n");
        expectError(result, ErrorCode::S003, 1, 1);
    }
}

LUSH_TEST(command_host_value_function_lifetime) {
    Env env;
    {
        Registry registry;
        CommandSpec spec = makeSpec("twice", ReturnKind::Value);
        spec.params.push_back(makeParam("x", ParamType::Number));
        const auto registered = registry.registerFunction(
            std::move(spec), [](const std::vector<Value>& args,
                                const CommandContext&) {
                return CommandResult::ok(
                    Value::makeDouble(args[0].asDouble() * 2.0));
            });
        LUSH_CHECK(registered.ok());

        const auto stored = runWith(registry, env, "t = twice\n");
        expectNoError(stored);
    }

    const auto result = runWithBuiltins(env, "echo t(2)\n");
    expectOutput(result, "4\n");
}

LUSH_TEST(command_host_error_r010) {
    Registry registry;
    CommandSpec boom = makeSpec("boom");
    const auto registered = registry.registerCommand(
        std::move(boom),
        [](const std::vector<Value>&, const CommandContext&, Output&) {
            return CommandResult::failure("boom failed");
        });
    LUSH_CHECK(registered.ok());

    {
        Env env;
        const auto result = runWith(registry, env, "echo 1\nboom\n");
        checkDiagnostic(result, ErrorCode::R010, 2, 1);
        LUSH_CHECK_EQ(result.output, std::string("1\n"));
    }

    CommandSpec bad = makeSpec("badfn", ReturnKind::Value);
    bad.params.push_back(makeParam("x", ParamType::Int));
    const auto badRegistered = registry.registerFunction(
        std::move(bad),
        [](const std::vector<Value>&, const CommandContext&) {
            return CommandResult::failure("bad function");
        });
    LUSH_CHECK(badRegistered.ok());

    {
        Env env;
        const auto result = runWith(registry, env, "echo badfn(1)\n");
        checkDiagnostic(result, ErrorCode::R010, 1, 6);
        LUSH_CHECK_EQ(result.output, std::string());
    }
    {
        Env env;
        const auto result = runWith(registry, env, "badfn 1\n");
        checkDiagnostic(result, ErrorCode::R010, 1, 1);
        LUSH_CHECK_EQ(result.output, std::string());
    }
}

LUSH_TEST(command_registration_errors) {
    Registry registry;
    CommandSpec base;
    base.name = "base";
    base.params.push_back(makeParam("x", ParamType::Int));
    LUSH_CHECK(registry.registerCommand(std::move(base), okHandler).ok());

    const auto checkError = [](const auto& result, RegistrationError expected) {
        LUSH_CHECK(result.error.has_value());
        if (result.error) {
            LUSH_CHECK_EQ(static_cast<int>(*result.error),
                          static_cast<int>(expected));
        }
    };

    {
        CommandSpec spec;
        spec.name = "base";
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::DuplicateName);
    }
    {
        CommandSpec spec;
        spec.name = "if";
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::Keyword);
    }
    {
        CommandSpec spec;
        spec.name = "int";
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::ReservedBuiltin);
    }
    {
        CommandSpec spec;
        spec.name = "alias_conflict";
        spec.aliases.push_back("base");
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::DuplicateName);
    }
    {
        CommandSpec spec;
        spec.name = "optional_then_required";
        ParamSpec optional = makeOptionalNumberParam("x", Value::makeInt(0));
        ParamSpec required = makeParam("y", ParamType::Int);
        spec.params = {optional, required};
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::RequiredAfterOptional);
    }
    {
        CommandSpec spec;
        spec.name = "optional_without_default";
        ParamSpec optional = makeParam("x", ParamType::Int);
        optional.optional = true;
        spec.params.push_back(optional);
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::OptionalWithoutDefault);
    }
    {
        CommandSpec spec;
        spec.name = "variadic_optional";
        spec.params.push_back(
            makeOptionalNumberParam("x", Value::makeInt(0)));
        ParamSpec variadic = makeParam("rest", ParamType::Any);
        variadic.variadic = true;
        spec.params.push_back(variadic);
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::VariadicOptional);
    }
    {
        CommandSpec spec;
        spec.name = "variadic_not_last";
        ParamSpec variadic = makeParam("rest", ParamType::Any);
        variadic.variadic = true;
        spec.params.push_back(variadic);
        spec.params.push_back(makeParam("x", ParamType::Int));
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::VariadicNotLast);
    }
    {
        CommandSpec spec;
        spec.name = "callable_default";
        ParamSpec optional = makeParam("x", ParamType::Any);
        optional.optional = true;
        optional.defaultValue = Value::makeCallable({});
        spec.params.push_back(optional);
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::DefaultTypeMismatch);
    }
    {
        CommandSpec spec;
        spec.name = "bad_default";
        ParamSpec optional = makeParam("x", ParamType::Int);
        optional.optional = true;
        optional.defaultValue = Value::makeString("x");
        spec.params.push_back(optional);
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::DefaultTypeMismatch);
    }
    {
        CommandSpec spec;
        spec.name = "bad_dim";
        ParamSpec param = makeParam("x", ParamType::Int);
        param.dim = 2;
        spec.params.push_back(param);
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::InvalidDim);
    }
    {
        CommandSpec spec;
        spec.name = "duplicate_param";
        spec.params.push_back(makeParam("x", ParamType::Int));
        spec.params.push_back(makeParam("x", ParamType::Int));
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::DuplicateParam);
    }
    {
        CommandSpec spec = makeSpec("interactive_value", ReturnKind::Value);
        spec.interactive = true;
        checkError(
            registry.registerFunction(
                std::move(spec),
                [](const std::vector<Value>&, const CommandContext&) {
                    return CommandResult::done();
                }),
            RegistrationError::InteractiveValue);
    }
    {
        CommandSpec spec = makeSpec("empty_handler");
        checkError(registry.registerCommand(std::move(spec), {}),
                   RegistrationError::EmptyHandler);
    }
    {
        CommandSpec spec = makeSpec("wrong_kind", ReturnKind::Value);
        checkError(registry.registerCommand(std::move(spec), okHandler),
                   RegistrationError::ReturnKindMismatch);
    }
}

LUSH_TEST(command_registration_defaults) {
    Registry registry;
    CommandSpec spec = makeSpec("defaulted", ReturnKind::Value);
    spec.params.push_back(
        makeOptionalNumberParam("x", Value::makeInt(0)));
    const auto registered = registry.registerFunction(
        std::move(spec), [](const std::vector<Value>& args,
                            const CommandContext&) {
            return CommandResult::ok(args[0]);
        });
    LUSH_CHECK(registered.ok());

    const auto* signature = registry.findSignature("defaulted");
    LUSH_CHECK(signature != nullptr);
    if (signature) {
        LUSH_CHECK_EQ(signature->required, std::size_t{0});
        LUSH_CHECK_EQ(signature->maxFixed, std::size_t{1});
        LUSH_CHECK(signature->params[0].defaultValue.has_value());
        if (signature->params[0].defaultValue) {
            LUSH_CHECK(signature->params[0].defaultValue->isDouble());
        }
    }

    {
        Env env;
        const auto result = runWith(registry, env, "echo defaulted()\n");
        expectOutput(result, "0\n");
    }
    {
        Env env;
        const auto result = runWith(registry, env, "echo defaulted(2)\n");
        expectOutput(result, "2\n");
    }
    {
        Env env;
        const auto result = runWith(registry, env, "defaulted\n");
        expectNoError(result);
        LUSH_CHECK_EQ(result.output, std::string());
    }

    CommandSpec pointSpec = makeSpec("point_default", ReturnKind::Value);
    ParamSpec point = makePointParam("p");
    point.optional = true;
    point.defaultValue =
        Value::makeList(ListValue(std::vector<Value>{Value::makeInt(0),
                                                    Value::makeInt(0)}));
    pointSpec.params.push_back(point);
    const auto pointRegistered = registry.registerFunction(
        std::move(pointSpec),
        [](const std::vector<Value>& args, const CommandContext&) {
            return CommandResult::ok(args[0]);
        });
    LUSH_CHECK(pointRegistered.ok());

    const auto* pointSignature = registry.findSignature("point_default");
    LUSH_CHECK(pointSignature != nullptr);
    if (pointSignature) {
        LUSH_CHECK(pointSignature->params[0].defaultValue.has_value());
        if (pointSignature->params[0].defaultValue) {
            LUSH_CHECK(pointSignature->params[0].defaultValue->isPoint());
        }
    }

    {
        Env env;
        const auto result = runWith(registry, env, "echo point_default()\n");
        expectOutput(result, "(0, 0)\n");
    }
}

LUSH_TEST(command_variadic_host) {
    Registry registry;
    CommandSpec spec = makeSpec("collect");
    ParamSpec rest = makeParam("rest", ParamType::Any);
    rest.variadic = true;
    spec.params.push_back(rest);
    const auto registered = registry.registerCommand(
        std::move(spec),
        [](const std::vector<Value>& args, const CommandContext&,
           Output& output) {
            output.write(std::to_string(args.size()) + "\n");
            return CommandResult::done();
        });
    LUSH_CHECK(registered.ok());

    Env env;
    const auto result = runWith(registry, env, "collect\ncollect 1 2\n");
    expectOutput(result, "0\n2\n");
}

LUSH_TEST(command_host_value_arity_statement_vs_expression) {
    Registry registry;
    CommandSpec spec = makeSpec("twice", ReturnKind::Value);
    spec.params.push_back(makeParam("x", ParamType::Number));
    LUSH_CHECK(registry.registerFunction(
                   std::move(spec),
                   [](const std::vector<Value>& args,
                      const CommandContext&) {
                       return CommandResult::ok(
                           Value::makeDouble(args[0].asDouble() * 2.0));
                   })
                   .ok());

    {
        Env env;
        const auto result = runWith(registry, env, "twice\n");
        expectError(result, ErrorCode::S002, 1, 1);
    }
    {
        Env env;
        const auto result = runWith(registry, env, "echo twice()\n");
        expectError(result, ErrorCode::R002, 1, 11);
    }
}

} // namespace

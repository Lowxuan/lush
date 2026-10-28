#include "lush/driver/driver.h"

#include "lush/command/session.h"
#include "lush/core/ast.h"
#include "lush/core/eval.h"
#include "lush/core/lexer.h"
#include "lush/core/parser.h"
#include "lush/core/validate.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// REPL/脚本执行状态机实现。
//
// 一次输入可能经历：拼接 pending 行 → Lexer → Parser → validate → eval。
// NeedMore 表示语法输入未完成；NeedInput 表示命令进入交互补参。
// pendingBuffer 只保存尚未形成完整输入的文本。
namespace lush::driver {
namespace {

// 空行/纯水平空白行；REPL 中用于取消未完成的 def body 等。
bool isBlankLine(std::string_view line) {
    for (char character : line) {
        if (character != ' ' && character != '\t' && character != '\r') {
            return false;
        }
    }
    return true;
}

bool isCommentOnlyLine(std::string_view line) {
    std::size_t index = 0;
    while (index < line.size() &&
           (line[index] == ' ' || line[index] == '\t' ||
            line[index] == '\r')) {
        ++index;
    }
    return index < line.size() && line[index] == '#';
}

void appendLine(std::string& buffer, std::string_view line) {
    buffer.append(line.data(), line.size());
    buffer.push_back('\n');
}

bool isSingleTopLevelCommand(const core::Program& program,
                             const core::Command*& command) {
    command = nullptr;
    for (const core::StmtPtr& statement : program.statements) {
        if (statement->kind() == core::NodeKind::EmptyStmt) {
            if (command == nullptr) {
                return false;
            }
            continue;
        }
        if (command != nullptr) {
            return false;
        }
        if (statement->kind() != core::NodeKind::Command) {
            return false;
        }
        command = static_cast<const core::Command*>(statement.get());
    }
    return command != nullptr;
}

core::Diagnostic makeDiagnostic(core::ErrorCode code, core::Span span,
                                std::string message, core::SourceId source) {
    return core::Diagnostic{code, span, std::move(message), {}, source};
}

command::NeedInput currentInput(const command::Session& session) {
    const command::NeedInput* input = session.currentInput();
    return input != nullptr ? *input : command::NeedInput{};
}

} // namespace

Driver::Driver(core::Env& env, command::Registry& registry,
               core::Output& output)
    : env_(env), registry_(registry), output_(output) {}

DriverResult Driver::makeError(std::optional<core::Diagnostic> diagnostic,
                               core::SourceId sourceId, bool sessionActive,
                               std::optional<InputOrigin> origin,
                               std::optional<command::NeedInput> input) {
    DriverResult result;
    result.status = DriverStatus::Error;
    result.sessionActive = sessionActive;
    result.origin = origin;
    result.input = std::move(input);

    if (diagnostic) {
        if (diagnostic->source == core::kNoSource &&
            sourceId != core::kNoSource) {
            diagnostic->source = sourceId;
        }
        result.source = sources_.find(diagnostic->source);
        if (!result.source && sourceId != core::kNoSource) {
            diagnostic->source = sourceId;
            result.source = sources_.find(sourceId);
        }
        result.diagnostic = std::move(diagnostic);
    } else {
        result.source = sources_.find(sourceId);
    }
    return result;
}

DriverResult Driver::makeNeedInput(command::NeedInput input,
                                   bool sessionActive) {
    DriverResult result;
    result.status = DriverStatus::NeedInput;
    result.input = std::move(input);
    result.sessionActive = sessionActive;
    return result;
}

DriverResult Driver::clearTerminalState(DriverResult result) {
    if (result.status == DriverStatus::Done ||
        result.status == DriverStatus::Cancelled ||
        (result.status == DriverStatus::Error && !result.sessionActive)) {
        sources_.clear();
        pendingBuffer_.clear();
        pendingReason_.reset();
    }
    return result;
}

DriverResult Driver::finishSession() {
    if (!session_) {
        return DriverResult{};
    }

    const core::SourceId commandSource = initialSource_;
    core::CommandContext context;
    context.nameSpan = commandNameSpan_;
    context.nameSource = commandSource;
    context.argSpans = session_->spans();
    context.argSources = session_->sources();

    std::optional<core::Diagnostic> diagnostic = registry_.invokeCommand(
        commandName_, session_->values(), output_, context);

    session_.reset();
    commandName_.clear();
    initialSource_ = core::kNoSource;

    if (diagnostic) {
        if (diagnostic->source == core::kNoSource) {
            diagnostic->source = commandSource;
        }
        return makeError(std::move(diagnostic), commandSource, false);
    }
    return DriverResult{};
}

// 处理已解析程序：决定是否进入会话、做静态校验、会话预求值或执行。
// allowFunctionRedefinition 在 REPL 中为 true，脚本中为 false。
DriverResult Driver::processParsed(core::Program& program,
                                  core::SourceId sourceId,
                                  bool allowFunctionRedefinition) {
    const core::Command* command = nullptr;
    const bool single = isSingleTopLevelCommand(program, command);

    bool allowSession = false;
    if (single && command != nullptr) {
        const std::optional<core::CallableInfo> info =
            registry_.callableInfo(command->name.text);
        allowSession = info.has_value() && info->interactive &&
                       command->args.size() < info->required;
    }

    core::ValidationResult validation = core::validate(
        program, registry_,
        core::ValidationOptions{allowSession, allowFunctionRedefinition,
                                &env_});
    if (validation.diagnostic) {
        return makeError(std::move(validation.diagnostic), sourceId, false);
    }

    if (allowSession && command != nullptr) {
        const command::Signature* signature =
            registry_.findSignature(command->name.text);
        if (signature == nullptr) {
            return makeError(
                makeDiagnostic(core::ErrorCode::S001, command->name.span,
                               "未知命令 " + command->name.text, sourceId),
                sourceId, false);
        }

        std::vector<core::Value> values;
        std::vector<core::Span> spans;
        std::vector<core::SourceId> sources;
        values.reserve(command->args.size());
        spans.reserve(command->args.size());
        sources.reserve(command->args.size());

        core::EvalContext eval{env_, output_, registry_, sourceId};
        for (const core::ExprPtr& argument : command->args) {
            core::EvalExpressionResult value =
                core::evalExpression(*argument, eval);
            if (!value.ok()) {
                return makeError(std::move(value.diagnostic), sourceId,
                                 false);
            }
            values.push_back(std::move(*value.value));
            spans.push_back(argument->span);
            sources.push_back(sourceId);
        }

        command::Session session(*signature);
        command::SessionResult started =
            session.start(values, spans, sources);
        if (started.status == command::SessionStatus::Error) {
            return makeError(std::move(started.diagnostic), sourceId, false);
        }

        session_ = std::move(session);
        initialSource_ = sourceId;
        commandName_ = command->name.text;
        commandNameSpan_ = command->name.span;

        if (started.status == command::SessionStatus::NeedInput) {
            return makeNeedInput(*started.input, true);
        }
        return finishSession();
    }

    core::EvalContext eval{env_, output_, registry_, sourceId};
    core::EvalResult evaluated = core::evalProgram(program, eval);
    if (evaluated.diagnostic) {
        return makeError(std::move(evaluated.diagnostic), sourceId, false);
    }
    return DriverResult{};
}

// 重新解析 pendingBuffer；完整输入交给 processParsed，否则保留 Incomplete。
DriverResult Driver::parsePendingBuffer() {
    const std::string text = pendingBuffer_;
    core::Source source(text);
    core::Lexer lexer(source);
    core::LexResult lex = lexer.tokenize();

    if (lex.diagnostic) {
        const core::SourceId sourceId = sources_.add(text);
        pendingBuffer_.clear();
        pendingReason_.reset();
        return makeError(std::move(lex.diagnostic), sourceId, false);
    }

    core::ParseResult parsed =
        core::parse(lex, core::ParseOptions::Repl);
    if (parsed.status == core::ParseStatus::Incomplete) {
        pendingReason_ = parsed.incompleteReason;
        DriverResult result;
        result.status = DriverStatus::NeedMore;
        result.incomplete = parsed.incompleteReason;
        return result;
    }
    if (parsed.status != core::ParseStatus::Ok || !parsed.program) {
        const core::SourceId sourceId = sources_.add(text);
        std::optional<core::Diagnostic> diagnostic =
            std::move(parsed.diagnostic);
        if (!diagnostic) {
            diagnostic = makeDiagnostic(
                core::ErrorCode::P002, core::Span{text.size(), text.size()},
                "意外的结束", sourceId);
        }
        pendingBuffer_.clear();
        pendingReason_.reset();
        return makeError(std::move(diagnostic), sourceId, false);
    }

    const core::SourceId sourceId = sources_.add(text);
    pendingBuffer_.clear();
    pendingReason_.reset();
    return processParsed(*parsed.program, sourceId, true);
}

DriverResult Driver::processSessionLine(std::string_view line) {
    if (!session_) {
        return DriverResult{};
    }

    if (isBlankLine(line)) {
        const std::string text = std::string(line) + "\n";
        const core::SourceId sourceId = sources_.add(text);
        core::Diagnostic diagnostic = makeDiagnostic(
            core::ErrorCode::R011, core::Span{0, 0}, "用户取消", sourceId);
        session_->cancel();
        session_.reset();
        commandName_.clear();
        initialSource_ = core::kNoSource;

        DriverResult result;
        result.status = DriverStatus::Cancelled;
        result.diagnostic = std::move(diagnostic);
        result.source = sources_.find(sourceId);
        result.cancelReason = CancelReason::EmptyRequired;
        result.origin = InputOrigin::Text;
        return result;
    }

    if (isCommentOnlyLine(line)) {
        return makeNeedInput(currentInput(*session_), true);
    }

    const std::string text = std::string(line) + "\n";
    const core::SourceId sourceId = sources_.add(text);
    core::Source source(text);
    core::Lexer lexer(source);
    core::LexResult lex = lexer.tokenize();
    if (lex.diagnostic) {
        return makeError(std::move(lex.diagnostic), sourceId, true,
                         std::nullopt, currentInput(*session_));
    }

    std::size_t limit = lex.tokens.size();
    while (limit > 0 &&
           (lex.tokens[limit - 1].kind == core::TokenKind::EndOfFile ||
            lex.tokens[limit - 1].kind == core::TokenKind::Newline)) {
        --limit;
    }
    if (limit == 0) {
        return makeError(
            makeDiagnostic(core::ErrorCode::P002, core::Span{0, 0},
                           "意外的结束", sourceId),
            sourceId, true, std::nullopt, currentInput(*session_));
    }

    core::ExprParseResult parsed = core::parseWord(
        lex, core::TokenRange{0, limit}, core::WordEnd::InputEnd,
        core::ParseOptions::Script);
    if (parsed.status != core::ParseStatus::Ok || !parsed.expression) {
        std::optional<core::Diagnostic> diagnostic =
            std::move(parsed.diagnostic);
        if (!diagnostic) {
            diagnostic = makeDiagnostic(
                core::ErrorCode::P002, core::Span{text.size(), text.size()},
                "意外的结束", sourceId);
        }
        return makeError(std::move(diagnostic), sourceId, true,
                         std::nullopt, currentInput(*session_));
    }

    core::ValidationResult validation =
        core::validateExpression(*parsed.expression, registry_);
    if (validation.diagnostic) {
        return makeError(std::move(validation.diagnostic), sourceId, true,
                         std::nullopt, currentInput(*session_));
    }

    core::EvalContext eval{env_, output_, registry_, sourceId};
    core::EvalExpressionResult value =
        core::evalExpression(*parsed.expression, eval);
    if (!value.ok()) {
        return makeError(std::move(value.diagnostic), sourceId, true,
                         std::nullopt, currentInput(*session_));
    }

    command::SessionResult submitted = session_->submit(
        std::move(*value.value), parsed.expression->span, sourceId);
    switch (submitted.status) {
    case command::SessionStatus::NeedInput:
        return makeNeedInput(*submitted.input, true);
    case command::SessionStatus::Ready:
        return finishSession();
    case command::SessionStatus::Error:
        return makeError(std::move(submitted.diagnostic), sourceId, true,
                         std::nullopt, submitted.input);
    case command::SessionStatus::Cancelled:
        session_.reset();
        commandName_.clear();
        initialSource_ = core::kNoSource;
        {
            DriverResult result;
            result.status = DriverStatus::Cancelled;
            result.cancelReason = CancelReason::Session;
            return result;
        }
    }
    return DriverResult{};
}

// 脚本入口：整体解析、校验，再一次性执行；不进入交互会话。
DriverResult Driver::runScript(std::string text) {
    const core::SourceId sourceId = sources_.add(text);
    core::Source source(text);
    core::Lexer lexer(source);
    core::LexResult lex = lexer.tokenize();
    if (lex.diagnostic) {
        return clearTerminalState(
            makeError(std::move(lex.diagnostic), sourceId, false));
    }

    core::ParseResult parsed =
        core::parse(lex, core::ParseOptions::Script);
    if (parsed.status != core::ParseStatus::Ok || !parsed.program) {
        std::optional<core::Diagnostic> diagnostic =
            std::move(parsed.diagnostic);
        if (!diagnostic) {
            diagnostic = makeDiagnostic(
                core::ErrorCode::P002, core::Span{text.size(), text.size()},
                "意外的结束", sourceId);
        }
        return clearTerminalState(
            makeError(std::move(diagnostic), sourceId, false));
    }

    core::ValidationResult validation =
        core::validate(*parsed.program, registry_,
                       core::ValidationOptions{false, false, &env_});
    if (validation.diagnostic) {
        return clearTerminalState(
            makeError(std::move(validation.diagnostic), sourceId, false));
    }

    core::EvalContext eval{env_, output_, registry_, sourceId};
    core::EvalResult evaluated = core::evalProgram(*parsed.program, eval);
    if (evaluated.diagnostic) {
        return clearTerminalState(
            makeError(std::move(evaluated.diagnostic), sourceId, false));
    }
    return clearTerminalState(DriverResult{});
}

// REPL 行入口；pending 状态下空行会取消输入（取消原因由状态决定）。
DriverResult Driver::feedLine(std::string_view line) {
    if (session_) {
        return clearTerminalState(processSessionLine(line));
    }

    if (!pendingBuffer_.empty()) {
        if (isBlankLine(line)) {
            if (pendingReason_ == core::IncompleteReason::NeedStatement ||
                pendingReason_ ==
                    core::IncompleteReason::TrailingContinuation) {
                pendingBuffer_.clear();
                pendingReason_.reset();
                DriverResult result;
                result.status = DriverStatus::Cancelled;
                result.cancelReason = CancelReason::PendingInput;
                return clearTerminalState(result);
            }
            pendingBuffer_.push_back('\n');
        } else {
            appendLine(pendingBuffer_, line);
        }
        return clearTerminalState(parsePendingBuffer());
    }

    if (isBlankLine(line) || isCommentOnlyLine(line)) {
        return DriverResult{};
    }

    pendingBuffer_.assign(line.data(), line.size());
    pendingBuffer_.push_back('\n');
    return clearTerminalState(parsePendingBuffer());
}

DriverResult Driver::submitPoint(core::PointValue point) {
    if (!session_) {
        return makeError(
            makeDiagnostic(core::ErrorCode::R010, core::Span{},
                           "当前没有交互会话", core::kNoSource),
            core::kNoSource, false, InputOrigin::DirectPoint);
    }

    command::SessionResult submitted =
        session_->submit(core::Value::makePoint(point), core::Span{},
                         core::kNoSource);
    switch (submitted.status) {
    case command::SessionStatus::NeedInput:
        return makeNeedInput(*submitted.input, true);
    case command::SessionStatus::Ready:
        return clearTerminalState(finishSession());
    case command::SessionStatus::Error: {
        std::optional<core::Diagnostic> diagnostic =
            std::move(submitted.diagnostic);
        if (diagnostic) {
            diagnostic->message =
                "直接提交的 Point: " + diagnostic->message;
        }
        return makeError(std::move(diagnostic), core::kNoSource, true,
                         InputOrigin::DirectPoint, submitted.input);
    }
    case command::SessionStatus::Cancelled:
        session_.reset();
        commandName_.clear();
        initialSource_ = core::kNoSource;
        {
            DriverResult result;
            result.status = DriverStatus::Cancelled;
            result.cancelReason = CancelReason::Session;
            return clearTerminalState(result);
        }
    }
    return DriverResult{};
}

DriverResult Driver::cancel() {
    if (session_) {
        session_->cancel();
        session_.reset();
        commandName_.clear();
        initialSource_ = core::kNoSource;
        DriverResult result;
        result.status = DriverStatus::Cancelled;
        result.cancelReason = CancelReason::Session;
        return clearTerminalState(result);
    }
    if (!pendingBuffer_.empty()) {
        pendingBuffer_.clear();
        pendingReason_.reset();
        DriverResult result;
        result.status = DriverStatus::Cancelled;
        result.cancelReason = CancelReason::PendingInput;
        return clearTerminalState(result);
    }
    DriverResult result;
    result.status = DriverStatus::Cancelled;
    return result;
}

DriverResult Driver::endInput() {
    if (session_) {
        session_->cancel();
        session_.reset();
        commandName_.clear();
        initialSource_ = core::kNoSource;
        DriverResult result;
        result.status = DriverStatus::Cancelled;
        result.cancelReason = CancelReason::Session;
        return clearTerminalState(result);
    }

    if (pendingBuffer_.empty()) {
        return DriverResult{};
    }

    const std::string text = pendingBuffer_;
    core::Source source(text);
    core::Lexer lexer(source);
    core::LexResult lex = lexer.tokenize();
    if (lex.diagnostic) {
        const core::SourceId sourceId = sources_.add(text);
        pendingBuffer_.clear();
        pendingReason_.reset();
        return clearTerminalState(
            makeError(std::move(lex.diagnostic), sourceId, false));
    }

    core::ParseResult parsed =
        core::parse(lex, core::ParseOptions::Script);
    if (parsed.status != core::ParseStatus::Ok || !parsed.program) {
        const core::SourceId sourceId = sources_.add(text);
        std::optional<core::Diagnostic> diagnostic =
            std::move(parsed.diagnostic);
        if (!diagnostic) {
            diagnostic = makeDiagnostic(
                core::ErrorCode::P002, core::Span{text.size(), text.size()},
                "意外的结束", sourceId);
        }
        pendingBuffer_.clear();
        pendingReason_.reset();
        return clearTerminalState(
            makeError(std::move(diagnostic), sourceId, false));
    }

    const core::SourceId sourceId = sources_.add(text);
    pendingBuffer_.clear();
    pendingReason_.reset();
    return clearTerminalState(makeError(
        makeDiagnostic(core::ErrorCode::P002,
                       core::Span{text.size(), text.size()}, "意外的结束",
                       sourceId),
        sourceId, false));
}

} // namespace lush::driver

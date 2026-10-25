#include "lush/command/session.h"

#include <utility>

namespace lush::command {

Session::Session(Signature signature) : signature_(std::move(signature)) {}

SessionResult Session::makeNeedInput() {
    SessionResult result;
    if (next_ >= signature_.required) {
        result.status = SessionStatus::Ready;
        result.active = active_;
        return result;
    }

    const ParamSpec& param = signature_.params[next_];
    NeedInput input;
    input.prompt = param.hint.empty() ? param.name : param.hint;
    input.type = param.type;
    input.dim = param.dim;
    input.index = next_;
    input_ = input;

    result.status = SessionStatus::NeedInput;
    result.input = input;
    result.active = active_;
    return result;
}

SessionResult Session::makeError(core::Diagnostic diagnostic, bool active) {
    SessionResult result;
    result.status = SessionStatus::Error;
    result.diagnostic = std::move(diagnostic);
    result.active = active;
    if (active && next_ < signature_.required) {
        SessionResult next = makeNeedInput();
        result.input = std::move(next.input);
    }
    return result;
}

bool Session::appendValue(core::Value value, core::Span span,
                          core::SourceId source, SessionResult& result) {
    const ParamSpec& param = signature_.params[next_];
    ParamBindResult bound = bindParam(param, value);
    if (!bound.value) {
        core::Diagnostic diagnostic;
        diagnostic.code = bound.error.value_or(core::ErrorCode::R002);
        diagnostic.span = span;
        diagnostic.message = std::move(bound.message);
        diagnostic.source = source;
        result = makeError(std::move(diagnostic), true);
        return false;
    }

    values_.push_back(std::move(*bound.value));
    spans_.push_back(span);
    sources_.push_back(source);
    ++next_;
    return true;
}

SessionResult Session::start(
    const std::vector<core::Value>& initialArgs,
    const std::vector<core::Span>& initialSpans,
    const std::vector<core::SourceId>& initialSources) {
    values_.clear();
    spans_.clear();
    sources_.clear();
    input_.reset();
    next_ = 0;
    active_ = true;

    if (initialArgs.size() > signature_.required) {
        core::Diagnostic diagnostic;
        diagnostic.code = core::ErrorCode::S002;
        diagnostic.message = "交互会话收到的参数过多";
        active_ = false;
        return makeError(std::move(diagnostic), false);
    }

    for (std::size_t index = 0; index < initialArgs.size(); ++index) {
        const core::Span span =
            index < initialSpans.size() ? initialSpans[index] : core::Span{};
        const core::SourceId source =
            index < initialSources.size() ? initialSources[index]
                                          : core::kNoSource;
        SessionResult error;
        if (!appendValue(initialArgs[index], span, source, error)) {
            active_ = false;
            error.active = false;
            error.input.reset();
            return error;
        }
    }

    return makeNeedInput();
}

SessionResult Session::submit(core::Value value, core::Span span,
                              core::SourceId source) {
    if (!active_ || next_ >= signature_.required) {
        core::Diagnostic diagnostic;
        diagnostic.code = core::ErrorCode::R010;
        diagnostic.message = "交互会话已经结束";
        return makeError(std::move(diagnostic), false);
    }

    SessionResult result;
    if (!appendValue(std::move(value), span, source, result)) {
        return result;
    }
    return makeNeedInput();
}

SessionResult Session::cancel() {
    active_ = false;
    input_.reset();
    SessionResult result;
    result.status = SessionStatus::Cancelled;
    result.active = false;
    return result;
}

} // namespace lush::command

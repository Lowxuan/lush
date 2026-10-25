#pragma once

#include "lush/command/registry.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lush::command {

struct NeedInput {
    std::string prompt;
    ParamType type = ParamType::Any;
    int dim = 0;
    std::size_t index = 0;
};

enum class SessionStatus {
    NeedInput,
    Ready,
    Error,
    Cancelled,
};

struct SessionResult {
    SessionStatus status = SessionStatus::Error;
    std::optional<NeedInput> input;
    std::optional<core::Diagnostic> diagnostic;
    bool active = false;
};

// Collects only the required parameters of an interactive command. Optional
// parameters are filled by the normal command binder when the session is
// ready. Session does not parse or evaluate anything; the driver does that.
class Session {
public:
    explicit Session(Signature signature);

    SessionResult start(const std::vector<core::Value>& initialArgs,
                        const std::vector<core::Span>& initialSpans,
                        const std::vector<core::SourceId>& initialSources);

    SessionResult submit(core::Value value, core::Span span,
                         core::SourceId source);
    SessionResult cancel();

    bool active() const noexcept {
        return active_;
    }

    const std::vector<core::Value>& values() const noexcept {
        return values_;
    }

    const std::vector<core::Span>& spans() const noexcept {
        return spans_;
    }

    const std::vector<core::SourceId>& sources() const noexcept {
        return sources_;
    }

    const NeedInput* currentInput() const noexcept {
        return input_ ? &*input_ : nullptr;
    }

private:
    SessionResult makeNeedInput();
    SessionResult makeError(core::Diagnostic diagnostic, bool active);
    bool appendValue(core::Value value, core::Span span,
                     core::SourceId source, SessionResult& result);

    Signature signature_;
    std::vector<core::Value> values_;
    std::vector<core::Span> spans_;
    std::vector<core::SourceId> sources_;
    std::optional<NeedInput> input_;
    std::size_t next_ = 0;
    bool active_ = false;
};

} // namespace lush::command

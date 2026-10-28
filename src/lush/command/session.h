#pragma once

#include "lush/command/registry.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lush::command {

// 当前需要用户补充的必选参数；不包含 optional/default/sourceId。
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

// 交互命令的必选参数状态机。
//
// 只收集 required 参数；optional 参数在会话完成后由普通 bind 逻辑补默认
// 值。Session 不解析、不求值、不写输出，输入行处理由 Driver 负责。
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

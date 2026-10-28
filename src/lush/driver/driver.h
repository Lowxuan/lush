#pragma once

#include "lush/command/registry.h"
#include "lush/driver/result.h"
#include "lush/driver/source_table.h"

#include <optional>
#include <string>
#include <string_view>

// 脚本/REPL 执行状态机。
//
// Driver 采用 pull 模型：调用方持续 feedLine() 或 runScript()，Driver
// 返回 Done/NeedMore/NeedInput/Error/Cancelled。它拥有 SourceTable 和
// 当前交互 Session，但不拥有 Env/Registry/Output。
namespace lush::core {
class Env;
class Output;
struct Program;
} // namespace lush::core

namespace lush::driver {

class Driver {
public:
    Driver(core::Env& env, command::Registry& registry, core::Output& output);

    // 解析并执行完整脚本；失败返回 Error，不进入会话。
    DriverResult runScript(std::string text);

    // 提交一行 REPL 输入；可能返回 NeedMore/NeedInput 继续收集。
    DriverResult feedLine(std::string_view line);
    DriverResult submitPoint(core::PointValue point);
    DriverResult cancel();
    DriverResult endInput();

    bool sessionActive() const noexcept {
        return session_.has_value();
    }

    bool pendingInput() const noexcept {
        return !pendingBuffer_.empty();
    }

private:
    DriverResult parsePendingBuffer();
    DriverResult processParsed(core::Program& program,
                               core::SourceId sourceId,
                               bool allowFunctionRedefinition);
    DriverResult processSessionLine(std::string_view line);
    DriverResult finishSession();
    DriverResult makeError(std::optional<core::Diagnostic> diagnostic,
                           core::SourceId sourceId, bool sessionActive,
                           std::optional<InputOrigin> origin = std::nullopt,
                           std::optional<command::NeedInput> input =
                               std::nullopt);
    DriverResult makeNeedInput(command::NeedInput input,
                               bool sessionActive);
    DriverResult clearTerminalState(DriverResult result);

    core::Env& env_;
    command::Registry& registry_;
    core::Output& output_;
    SourceTable sources_;

    std::string pendingBuffer_;
    std::optional<core::IncompleteReason> pendingReason_;

    std::optional<command::Session> session_;
    core::SourceId initialSource_ = core::kNoSource;
    std::string commandName_;
    core::Span commandNameSpan_{};
};

} // namespace lush::driver

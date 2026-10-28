#pragma once

#include "lush/command/registry.h"
#include "lush/driver/result.h"
#include "lush/driver/source_table.h"

#include <optional>
#include <string>
#include <string_view>

namespace lush::core {
class Env;
class Output;
struct Program;
} // namespace lush::core

namespace lush::driver {

class Driver {
public:
    Driver(core::Env& env, command::Registry& registry, core::Output& output);

    DriverResult runScript(std::string text);
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

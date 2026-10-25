#pragma once

#include "lush/command/session.h"
#include "lush/core/diagnostic.h"
#include "lush/core/parser.h"
#include "lush/core/source.h"

#include <memory>
#include <optional>

namespace lush::driver {

enum class DriverStatus {
    Done,
    NeedMore,
    NeedInput,
    Error,
    Cancelled,
};

enum class InputOrigin {
    Text,
    DirectPoint,
};

enum class CancelReason {
    None,
    PendingInput,
    Session,
    EmptyRequired,
};

struct DriverResult {
    DriverStatus status = DriverStatus::Done;
    std::optional<core::IncompleteReason> incomplete;
    std::optional<command::NeedInput> input;
    std::optional<core::Diagnostic> diagnostic;
    std::shared_ptr<const core::Source> source;
    std::optional<InputOrigin> origin;
    CancelReason cancelReason = CancelReason::None;
    bool sessionActive = false;
};

} // namespace lush::driver

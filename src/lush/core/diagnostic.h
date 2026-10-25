#pragma once

#include "lush/core/source.h"

#include <string>
#include <string_view>

namespace lush::core {

enum class ErrorCode {
    L001,
    L002,
    L003,
    L004,
    L005,
    L006,
    P001,
    P002,
    P003,
    P004,
    P005,
    P006,
    S001,
    S002,
    S003,
    S004,
    S005,
    S006,
    S007,
    S008,
    S009,
    R001,
    R002,
    R003,
    R004,
    R005,
    R006,
    R007,
    R008,
    R009,
    R010,
    R011,
    R012,
    R013,
};

std::string_view errorCodeName(ErrorCode code) noexcept;

struct Diagnostic {
    ErrorCode code = ErrorCode::L001;
    Span span{};
    std::string message;
    std::string help;
    // Optional source identity for host/session diagnostics. Core diagnostics
    // leave this as kNoSource; the driver supplies the active source.
    SourceId source = kNoSource;
};

// Formats a diagnostic in the shape defined by docs/spec.md 11.2.
std::string formatDiagnostic(const Source& source, const Diagnostic& diagnostic);

} // namespace lush::core

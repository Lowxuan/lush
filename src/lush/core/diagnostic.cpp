#include "lush/core/diagnostic.h"

#include "lush/core/utf8.h"

#include <algorithm>
#include <sstream>

namespace lush::core {

std::string_view errorCodeName(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::L001: return "L001";
    case ErrorCode::L002: return "L002";
    case ErrorCode::L003: return "L003";
    case ErrorCode::L004: return "L004";
    case ErrorCode::L005: return "L005";
    case ErrorCode::L006: return "L006";
    case ErrorCode::P001: return "P001";
    case ErrorCode::P002: return "P002";
    case ErrorCode::P003: return "P003";
    case ErrorCode::P004: return "P004";
    case ErrorCode::P005: return "P005";
    case ErrorCode::P006: return "P006";
    case ErrorCode::S001: return "S001";
    case ErrorCode::S002: return "S002";
    case ErrorCode::S003: return "S003";
    case ErrorCode::S004: return "S004";
    case ErrorCode::S005: return "S005";
    case ErrorCode::S006: return "S006";
    case ErrorCode::S007: return "S007";
    case ErrorCode::S008: return "S008";
    case ErrorCode::R001: return "R001";
    case ErrorCode::R002: return "R002";
    case ErrorCode::R003: return "R003";
    case ErrorCode::R004: return "R004";
    case ErrorCode::R005: return "R005";
    case ErrorCode::R006: return "R006";
    case ErrorCode::R007: return "R007";
    case ErrorCode::R008: return "R008";
    case ErrorCode::R009: return "R009";
    case ErrorCode::R010: return "R010";
    case ErrorCode::R011: return "R011";
    case ErrorCode::R012: return "R012";
    }
    return "????";
}

std::string formatDiagnostic(const Source& source, const Diagnostic& diagnostic) {
    const LineCol position = source.lineCol(diagnostic.span.start);
    const std::string_view lineText = source.lineText(position.line);
    const std::string lineNumber = std::to_string(position.line);
    const std::string gutter(lineNumber.size(), ' ');

    std::size_t caretColumn = position.column > 0 ? position.column - 1 : 0;
    const std::size_t lineLength = utf8Length(lineText);
    caretColumn = std::min(caretColumn, lineLength);

    std::ostringstream out;
    out << "error[" << errorCodeName(diagnostic.code) << "]: "
        << diagnostic.message << '\n';
    out << "  --> " << position.line << ':' << position.column << '\n';
    out << gutter << "  |\n";
    out << ' ' << lineNumber << " | " << lineText << '\n';
    out << gutter << "  | " << std::string(caretColumn, ' ') << '^';

    if (!diagnostic.help.empty()) {
        out << '\n' << gutter << "  = help: " << diagnostic.help;
    }
    out << '\n';

    return out.str();
}

} // namespace lush::core

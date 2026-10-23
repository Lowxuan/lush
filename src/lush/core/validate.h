#pragma once

#include "lush/core/ast.h"
#include "lush/core/diagnostic.h"

#include <optional>

namespace lush::core {

class NameCatalog;

struct ValidationResult {
    std::optional<Diagnostic> diagnostic;
};

ValidationResult validate(const Program& program, const NameCatalog& names);

} // namespace lush::core

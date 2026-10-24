#pragma once

#include "lush/core/ast.h"
#include "lush/core/diagnostic.h"

#include <optional>

namespace lush::core {

class NameCatalog;

struct ValidationOptions {
    // True only when the caller has established that the whole input is a
    // single top-level command in REPL mode and may enter an interactive
    // session.  Script validation and nested calls leave this false.
    bool allowInteractiveSession = false;
};

struct ValidationResult {
    std::optional<Diagnostic> diagnostic;
};

ValidationResult validate(const Program& program, const NameCatalog& names,
                          ValidationOptions options = {});

} // namespace lush::core

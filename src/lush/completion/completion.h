#pragma once

#include "lush/command/registry.h"
#include "lush/command/session.h"
#include "lush/core/cell.h"
#include "lush/core/source.h"
#include "lush/core/value.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lush::completion {

enum class CompletionContext {
    None,
    StatementStart,
    Expression,
    CommandArgument,
    ForIterable,
    ListElement,
    ParenExpression,
    CallArgument,
    Member,
    SessionArgument,
};

enum class CompletionKind {
    Keyword,
    Variable,
    Command,
    Function,
    TypeConstructor,
    Member,
    Literal,
    Syntax,
};

struct CompletionItem {
    std::string label;
    std::string insertText;
    CompletionKind kind = CompletionKind::Variable;
    std::string detail;
    std::optional<core::ValueType> valueType;
    int dim = 0;
    bool typeMismatch = false;
    core::Span replaceSpan{};
};

struct CompletionRequest {
    const core::Source& source;
    std::size_t cursor = 0; // raw byte offset
    const core::Env& env;
    const command::Registry& registry;
    std::optional<command::NeedInput> sessionInput;
};

struct CompletionResult {
    CompletionContext context = CompletionContext::None;
    core::Span replaceSpan{};
    std::vector<CompletionItem> items;
};

CompletionResult complete(const CompletionRequest& request);

} // namespace lush::completion

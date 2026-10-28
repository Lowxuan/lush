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

// 纯计算补全 API。
//
// 只分析 cursor 前缀，不执行表达式、不调用宿主、不写输出、不产生诊断。
// 返回的插入文本是纯文本；宿主决定是否追加空格/括号。
namespace lush::completion {

// cursor 所在语法位置；决定候选集合和类型过滤。
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

// 候选类别，用于宿主图标/排序；不参与语义判断。
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

// 单个补全候选；replaceSpan 是要被 insertText 替换的原始字节范围。
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

// 一次补全请求；引用均只在 complete() 调用期间有效。
struct CompletionRequest {
    const core::Source& source;
    std::size_t cursor = 0; // raw byte offset
    const core::Env& env;
    const command::Registry& registry;
    std::optional<command::NeedInput> sessionInput;
};

// 补全结果：上下文仅用于诊断/宿主展示，items 可以为空。
struct CompletionResult {
    CompletionContext context = CompletionContext::None;
    core::Span replaceSpan{};
    std::vector<CompletionItem> items;
};

// 根据 source/cursor/env/registry 计算候选；不抛异常、不产生副作用。
CompletionResult complete(const CompletionRequest& request);

} // namespace lush::completion

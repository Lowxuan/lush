#include "lush/completion/completion.h"

#include "lush/core/format.h"
#include "lush/core/lexer.h"
#include "lush/core/parser.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lush::completion {
namespace {

using core::SourceId;
using core::Span;
using core::Token;
using core::TokenKind;
using core::Value;
using core::ValueType;

struct TypeInfo {
    std::optional<ValueType> type;
    int dim = 0;
};

struct CallableDescriptor {
    command::Signature signature;
    std::string help;
    bool typeConstructor = false;
};

char asciiLower(char character) {
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

std::string asciiLower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char character : text) {
        result.push_back(asciiLower(character));
    }
    return result;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (asciiLower(left[index]) != asciiLower(right[index])) {
            return false;
        }
    }
    return true;
}

bool startsWithExact(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

bool startsWithIgnoreCase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (asciiLower(text[index]) != asciiLower(prefix[index])) {
            return false;
        }
    }
    return true;
}

bool isIdentifierLike(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:
    case TokenKind::KwIf:
    case TokenKind::KwElse:
    case TokenKind::KwWhile:
    case TokenKind::KwFor:
    case TokenKind::KwIn:
    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
    case TokenKind::KwRef:
    case TokenKind::KwBreak:
    case TokenKind::KwContinue:
    case TokenKind::KwDef:
    case TokenKind::KwReturn:
        return true;
    default:
        return false;
    }
}

bool isAtomicToken(TokenKind kind) {
    switch (kind) {
    case TokenKind::Int:
    case TokenKind::BoundaryInt:
    case TokenKind::Float:
    case TokenKind::String:
    case TokenKind::RightParen:
    case TokenKind::RightBracket:
        return true;
    default:
        return false;
    }
}

bool isStatementTerminator(TokenKind kind) {
    return kind == TokenKind::Newline || kind == TokenKind::Semicolon ||
           kind == TokenKind::LeftBrace || kind == TokenKind::RightBrace ||
           kind == TokenKind::KwElse;
}

bool isExpressionContinuation(TokenKind kind) {
    switch (kind) {
    case TokenKind::Assign:
    case TokenKind::Equal:
    case TokenKind::NotEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
    case TokenKind::AmpAmp:
    case TokenKind::PipePipe:
    case TokenKind::Question:
    case TokenKind::Colon:
    case TokenKind::Comma:
        return true;
    default:
        return false;
    }
}

bool isLeftDelimiter(TokenKind kind) {
    return kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket;
}

bool isRightDelimiter(TokenKind kind) {
    return kind == TokenKind::RightParen || kind == TokenKind::RightBracket;
}

bool matchingPair(TokenKind left, TokenKind right) {
    return (left == TokenKind::LeftParen && right == TokenKind::RightParen) ||
           (left == TokenKind::LeftBracket &&
            right == TokenKind::RightBracket);
}

std::string valueTypeLabel(ValueType type, int dim) {
    if (type == ValueType::Callable) {
        return "callable";
    }
    std::string label(core::valueTypeName(type));
    if (type == ValueType::Point && dim != 0) {
        label += " dim";
        label += std::to_string(dim);
    }
    return label;
}

std::string paramTypeLabel(command::ParamType type, int dim) {
    switch (type) {
    case command::ParamType::Int: return "int";
    case command::ParamType::Number: return "number";
    case command::ParamType::Point: {
        std::string label = "point";
        if (dim != 0) {
            label += " dim";
            label += std::to_string(dim);
        }
        return label;
    }
    case command::ParamType::String: return "string";
    case command::ParamType::Bool: return "bool";
    case command::ParamType::List: return "list";
    case command::ParamType::Any: return "any";
    }
    return "any";
}

std::string returnTypeLabel(const command::Signature& signature) {
    if (!signature.returnsValue) {
        return "void";
    }
    if (!signature.returnType) {
        return "value";
    }
    return valueTypeLabel(signature.returnType->type,
                          signature.returnType->dim);
}

std::string signatureText(const command::Signature& signature) {
    std::string result = signature.name;
    result.push_back('(');
    for (std::size_t index = 0; index < signature.params.size(); ++index) {
        if (index != 0) {
            result += ", ";
        }
        const command::ParamSpec& param = signature.params[index];
        result += param.name;
        result += ": ";
        result += paramTypeLabel(param.type, param.dim);
        if (param.variadic) {
            result += "...";
        } else if (param.optional) {
            result += "?";
        }
    }
    result += ") -> ";
    result += returnTypeLabel(signature);
    return result;
}

std::string expectedSuffix(const command::ParamSpec& expected) {
    std::string suffix = expected.hint;
    if (expected.optional && expected.defaultValue) {
        if (!suffix.empty()) {
            suffix += ' ';
        }
        suffix += "[default: ";
        suffix += core::formatValue(*expected.defaultValue);
        suffix += ']';
    }
    return suffix;
}

command::ParamSpec anyParam(std::string name) {
    command::ParamSpec param;
    param.name = std::move(name);
    param.type = command::ParamType::Any;
    return param;
}

CallableDescriptor makeBuiltin(
    std::string name, std::vector<std::string> aliases,
    std::vector<command::ParamSpec> params, bool variadic, bool returnsValue,
    std::optional<command::ReturnType> returnType, bool typeConstructor,
    std::string help) {
    CallableDescriptor descriptor;
    descriptor.signature.name = std::move(name);
    descriptor.signature.aliases = std::move(aliases);
    descriptor.signature.params = std::move(params);
    descriptor.signature.variadic = variadic;
    descriptor.signature.returnsValue = returnsValue;
    descriptor.signature.returnType = returnType;
    descriptor.signature.help = std::move(help);
    descriptor.help = descriptor.signature.help;
    descriptor.typeConstructor = typeConstructor;
    descriptor.signature.maxFixed = 0;
    descriptor.signature.required = 0;
    for (const command::ParamSpec& param : descriptor.signature.params) {
        if (param.variadic) {
            continue;
        }
        ++descriptor.signature.maxFixed;
        if (!param.optional) {
            ++descriptor.signature.required;
        }
    }
    return descriptor;
}

std::vector<CallableDescriptor> builtinDescriptors() {
    using command::ParamType;
    using command::ReturnType;
    std::vector<CallableDescriptor> result;

    result.push_back(makeBuiltin(
        "echo", {}, {anyParam("v")}, true, false, std::nullopt, false,
        "输出参数"));

    result.push_back(makeBuiltin(
        "int", {}, {anyParam("v")}, false, true,
        ReturnType{ValueType::Int, 0}, true, "转换为整数"));
    result.push_back(makeBuiltin(
        "double", {}, {anyParam("v")}, false, true,
        ReturnType{ValueType::Double, 0}, true, "转换为浮点数"));
    result.push_back(makeBuiltin(
        "bool", {}, {anyParam("v")}, false, true,
        ReturnType{ValueType::Bool, 0}, true, "转换为布尔值"));
    result.push_back(makeBuiltin(
        "string", {"str"}, {anyParam("v")}, false, true,
        ReturnType{ValueType::String, 0}, true, "转换为字符串"));
    result.push_back(makeBuiltin(
        "list", {}, {anyParam("v")}, false, true,
        ReturnType{ValueType::List, 0}, true, "转换为列表"));
    result.push_back(makeBuiltin(
        "point", {}, {anyParam("v")}, true, true,
        ReturnType{ValueType::Point, 0}, true, "构造 Point"));
    result.push_back(makeBuiltin(
        "type", {}, {anyParam("v")}, false, true,
        ReturnType{ValueType::Callable, 0}, false, "返回类型构造器"));
    result.push_back(makeBuiltin(
        "len", {}, {anyParam("v")}, false, true,
        ReturnType{ValueType::Int, 0}, false, "返回长度"));
    return result;
}

CallableDescriptor descriptorFromSignature(const command::Signature& signature) {
    CallableDescriptor descriptor;
    descriptor.signature = signature;
    descriptor.help = signature.help;
    return descriptor;
}

std::optional<CallableDescriptor> findDescriptor(
    std::string_view name, const command::Registry& registry) {
    for (const CallableDescriptor& descriptor : builtinDescriptors()) {
        if (equalsIgnoreCase(descriptor.signature.name, name)) {
            return descriptor;
        }
        for (const std::string& alias : descriptor.signature.aliases) {
            if (equalsIgnoreCase(alias, name)) {
                return descriptor;
            }
        }
    }
    if (const command::Signature* signature = registry.findSignature(name)) {
        return descriptorFromSignature(*signature);
    }
    return std::nullopt;
}

std::vector<CallableDescriptor> allDescriptors(
    const command::Registry& registry) {
    std::vector<CallableDescriptor> result = builtinDescriptors();
    for (const command::Signature& signature : registry.signatures()) {
        result.push_back(descriptorFromSignature(signature));
    }
    return result;
}

std::optional<command::ParamSpec> paramAt(const command::Signature& signature,
                                          std::size_t index) {
    if (index < signature.maxFixed && index < signature.params.size()) {
        return signature.params[index];
    }
    if (signature.variadic && !signature.params.empty()) {
        return signature.params.back();
    }
    return std::nullopt;
}

TypeInfo typeOfValue(const Value& value) {
    TypeInfo result;
    result.type = value.type();
    if (value.isPoint()) {
        result.dim = value.asPoint().dim;
    }
    return result;
}

TypeInfo returnTypeOf(const command::Signature& signature) {
    if (!signature.returnsValue || !signature.returnType) {
        return TypeInfo{};
    }
    return TypeInfo{signature.returnType->type, signature.returnType->dim};
}

TypeInfo memberType(const TypeInfo& receiver, std::string_view member) {
    if (!receiver.type) {
        return TypeInfo{};
    }
    if (*receiver.type == ValueType::Point) {
        if (member == "x" || member == "y" || member == "z") {
            return TypeInfo{ValueType::Double, 0};
        }
        return TypeInfo{};
    }
    if (*receiver.type == ValueType::List ||
        *receiver.type == ValueType::String) {
        if (member == "length") {
            return TypeInfo{ValueType::Int, 0};
        }
    }
    return TypeInfo{};
}

std::size_t findMatchingOpen(const std::vector<Token>& tokens,
                             std::size_t closeIndex, TokenKind left,
                             TokenKind right) {
    if (closeIndex >= tokens.size() || tokens[closeIndex].kind != right) {
        return static_cast<std::size_t>(-1);
    }
    std::size_t depth = 0;
    for (std::size_t index = closeIndex; index > 0;) {
        --index;
        if (tokens[index].kind == right) {
            ++depth;
        } else if (tokens[index].kind == left) {
            if (depth == 0) {
                return index;
            }
            --depth;
        }
    }
    return static_cast<std::size_t>(-1);
}

std::size_t countTopLevelCommas(const std::vector<Token>& tokens,
                                std::size_t openIndex,
                                std::size_t closeIndex) {
    std::size_t depth = 0;
    std::size_t commas = 0;
    for (std::size_t index = openIndex + 1; index < closeIndex; ++index) {
        if (isLeftDelimiter(tokens[index].kind)) {
            ++depth;
        } else if (isRightDelimiter(tokens[index].kind)) {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0 && tokens[index].kind == TokenKind::Comma) {
            ++commas;
        }
    }
    return commas;
}

TypeInfo inferTokenType(const Token& token, const core::Env& env,
                        const command::Registry& registry);

TypeInfo inferReceiverType(const std::vector<Token>& tokens,
                           std::size_t dotIndex, const core::Env& env,
                           const command::Registry& registry) {
    if (dotIndex == 0 || dotIndex > tokens.size()) {
        return TypeInfo{};
    }
    const Token& token = tokens[dotIndex - 1];
    if (token.kind == TokenKind::Identifier) {
        if (dotIndex >= 2 && tokens[dotIndex - 2].kind == TokenKind::Dot) {
            const TypeInfo receiver = inferReceiverType(
                tokens, dotIndex - 2, env, registry);
            return memberType(receiver, token.text);
        }
        if (const std::shared_ptr<core::Cell> cell = env.find(token.text)) {
            return typeOfValue(cell->read());
        }
        if (const std::optional<CallableDescriptor> descriptor =
                findDescriptor(token.text, registry)) {
            if (descriptor->signature.returnsValue) {
                return TypeInfo{ValueType::Callable, 0};
            }
        }
        return TypeInfo{};
    }

    if (token.kind == TokenKind::RightParen) {
        const std::size_t open = findMatchingOpen(
            tokens, dotIndex - 1, TokenKind::LeftParen,
            TokenKind::RightParen);
        if (open == static_cast<std::size_t>(-1)) {
            return TypeInfo{};
        }
        if (open > 0 && tokens[open - 1].kind == TokenKind::Identifier) {
            if (const std::optional<CallableDescriptor> descriptor =
                    findDescriptor(tokens[open - 1].text, registry)) {
                const TypeInfo result = returnTypeOf(descriptor->signature);
                if (result.type) {
                    return result;
                }
            }
        }
        const std::size_t commas =
            countTopLevelCommas(tokens, open, dotIndex - 1);
        if (commas >= 1 && commas <= 2) {
            return TypeInfo{ValueType::Point, static_cast<int>(commas + 1)};
        }
        if (commas == 0 && open + 2 == dotIndex) {
            return inferTokenType(tokens[open + 1], env, registry);
        }
        return TypeInfo{};
    }

    if (token.kind == TokenKind::RightBracket) {
        const std::size_t open = findMatchingOpen(
            tokens, dotIndex - 1, TokenKind::LeftBracket,
            TokenKind::RightBracket);
        if (open == static_cast<std::size_t>(-1)) {
            return TypeInfo{};
        }
        if (open == 0) {
            return TypeInfo{ValueType::List, 0};
        }
        const TokenKind before = tokens[open - 1].kind;
        if (before == TokenKind::Assign || before == TokenKind::Comma ||
            before == TokenKind::LeftParen ||
            before == TokenKind::LeftBracket ||
            isExpressionContinuation(before)) {
            return TypeInfo{ValueType::List, 0};
        }
        return TypeInfo{};
    }

    return inferTokenType(token, env, registry);
}

TypeInfo inferTokenType(const Token& token, const core::Env& env,
                        const command::Registry& registry) {
    switch (token.kind) {
    case TokenKind::Int:
    case TokenKind::BoundaryInt:
        return TypeInfo{ValueType::Int, 0};
    case TokenKind::Float:
        return TypeInfo{ValueType::Double, 0};
    case TokenKind::String:
        return TypeInfo{ValueType::String, 0};
    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
        return TypeInfo{ValueType::Bool, 0};
    case TokenKind::Identifier:
        if (const std::shared_ptr<core::Cell> cell = env.find(token.text)) {
            return typeOfValue(cell->read());
        }
        if (const std::optional<CallableDescriptor> descriptor =
                findDescriptor(token.text, registry)) {
            if (descriptor->signature.returnsValue) {
                return TypeInfo{ValueType::Callable, 0};
            }
        }
        return TypeInfo{};
    default:
        return TypeInfo{};
    }
}

std::size_t lastTokenEnd(const std::vector<Token>& tokens) {
    if (tokens.empty()) {
        return 0;
    }
    return tokens.back().offset + tokens.back().length;
}

bool hasWhitespaceBetween(std::string_view text, std::size_t begin,
                          std::size_t end) {
    if (begin >= end || end > text.size()) {
        return false;
    }
    for (std::size_t index = begin; index < end; ++index) {
        if (text[index] == ' ' || text[index] == '\t' ||
            text[index] == '\r') {
            return true;
        }
    }
    return false;
}

struct Structure {
    std::size_t statementStart = 0;
    std::vector<std::pair<TokenKind, std::size_t>> delimiters;
};

Structure structureOf(const std::vector<Token>& tokens) {
    Structure structure;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const TokenKind kind = tokens[index].kind;
        if (structure.delimiters.empty() && isStatementTerminator(kind)) {
            structure.statementStart = index + 1;
        }
        if (isLeftDelimiter(kind)) {
            structure.delimiters.emplace_back(kind, index);
        } else if (isRightDelimiter(kind)) {
            for (std::size_t depth = structure.delimiters.size();
                 depth > 0; --depth) {
                const auto& entry = structure.delimiters[depth - 1];
                if (matchingPair(entry.first, kind)) {
                    structure.delimiters.resize(depth - 1);
                    break;
                }
            }
        }
    }
    return structure;
}

struct PrefixInfo {
    std::optional<std::size_t> tokenIndex;
    std::string prefix;
    Span replace{};
};

PrefixInfo prefixOf(const std::vector<Token>& tokens,
                    std::string_view text, std::size_t cursor) {
    PrefixInfo info;
    info.replace = Span{cursor, cursor};
    if (tokens.empty()) {
        return info;
    }
    const Token& last = tokens.back();
    const std::size_t end = last.offset + last.length;
    if (end == cursor && isIdentifierLike(last.kind)) {
        info.tokenIndex = tokens.size() - 1;
        info.prefix = std::string(text.substr(last.offset,
                                              cursor - last.offset));
        info.replace = Span{last.offset, end};
    }
    return info;
}

bool matchesPrefix(std::string_view label, std::string_view prefix,
                   bool caseInsensitive) {
    return caseInsensitive ? startsWithIgnoreCase(label, prefix)
                           : startsWithExact(label, prefix);
}

void addItem(std::vector<CompletionItem>& items, std::string label,
             std::string insertText, CompletionKind kind, std::string detail,
             std::optional<ValueType> valueType, int dim, bool typeMismatch,
             Span replace) {
    CompletionItem item;
    item.label = std::move(label);
    item.insertText = std::move(insertText);
    item.kind = kind;
    item.detail = std::move(detail);
    item.valueType = valueType;
    item.dim = dim;
    item.typeMismatch = typeMismatch;
    item.replaceSpan = replace;
    items.push_back(std::move(item));
}

void addVariableItems(std::vector<CompletionItem>& items,
                      const core::Env& env, std::string_view prefix,
                      const std::optional<command::ParamSpec>& expected,
                      bool iterableOnly, Span replace) {
    for (const std::string& name : env.visibleNames()) {
        if (!matchesPrefix(name, prefix, false)) {
            continue;
        }
        const std::shared_ptr<core::Cell> cell = env.find(name);
        if (!cell) {
            continue;
        }
        const Value& value = cell->read();
        if (iterableOnly && !value.isList() && !value.isString()) {
            continue;
        }

        bool mismatch = false;
        if (expected && expected->type != command::ParamType::Any) {
            const command::ParamBindResult bound =
                command::bindParam(*expected, value);
            mismatch = !bound.value.has_value();
        }

        std::string detail =
            valueTypeLabel(value.type(), value.isPoint() ? value.asPoint().dim
                                                         : 0);
        if (mismatch) {
            detail += " (期望 ";
            detail += paramTypeLabel(expected->type, expected->dim);
            detail += ")";
        }
        addItem(items, name, name, CompletionKind::Variable,
                std::move(detail), value.type(),
                value.isPoint() ? value.asPoint().dim : 0, mismatch, replace);
    }
}

void addCallableItems(std::vector<CompletionItem>& items,
                      const command::Registry& registry,
                      std::string_view prefix, CompletionContext context,
                      const std::optional<command::ParamSpec>& expected,
                      Span replace) {
    const bool statement = context == CompletionContext::StatementStart;
    const bool expression = context == CompletionContext::Expression ||
                            context == CompletionContext::ParenExpression ||
                            context == CompletionContext::ListElement;
    const bool anyExpected =
        expected && expected->type == command::ParamType::Any;
    const bool compatibleOnly =
        context == CompletionContext::CommandArgument ||
        context == CompletionContext::CallArgument ||
        context == CompletionContext::SessionArgument;
    if (!statement && !expression && !(compatibleOnly && anyExpected)) {
        return;
    }

    for (const CallableDescriptor& descriptor : allDescriptors(registry)) {
        const bool returnsValue = descriptor.signature.returnsValue;
        if (!statement && !returnsValue) {
            continue;
        }
        if (compatibleOnly && !anyExpected) {
            continue;
        }

        const CompletionKind kind =
            !returnsValue ? CompletionKind::Command
                          : (descriptor.typeConstructor
                                 ? CompletionKind::TypeConstructor
                                 : CompletionKind::Function);
        std::string detail = signatureText(descriptor.signature);
        if (!descriptor.help.empty()) {
            detail += " — ";
            detail += descriptor.help;
        }

        std::vector<std::string> names;
        names.push_back(descriptor.signature.name);
        for (const std::string& alias : descriptor.signature.aliases) {
            if (!equalsIgnoreCase(alias, descriptor.signature.name)) {
                names.push_back(alias);
            }
        }
        for (const std::string& name : names) {
            if (!matchesPrefix(name, prefix, true)) {
                continue;
            }
            std::string itemDetail = detail;
            if (!equalsIgnoreCase(name, descriptor.signature.name)) {
                itemDetail = "别名 " + descriptor.signature.name + "；" +
                             itemDetail;
            }
            addItem(items, name, name, kind, std::move(itemDetail),
                    returnsValue ? std::optional<ValueType>(ValueType::Callable)
                                 : std::nullopt,
                    0, false, replace);
        }
    }
}

void addKeywordItems(std::vector<CompletionItem>& items,
                     const std::vector<Token>& tokens,
                     std::size_t statementStart, std::string_view prefix,
                     Span replace) {
    std::vector<std::string> keywords = {"if", "while", "for", "break",
                                         "continue"};
    if (statementStart > 0 &&
        tokens[statementStart - 1].kind == TokenKind::RightBrace) {
        keywords.push_back("else");
    }
    if (statementStart < tokens.size() &&
        tokens[statementStart].kind == TokenKind::KwFor) {
        bool hasIn = false;
        for (std::size_t index = statementStart + 1; index < tokens.size();
             ++index) {
            if (tokens[index].kind == TokenKind::KwIn) {
                hasIn = true;
                break;
            }
        }
        if (!hasIn && statementStart + 1 < tokens.size() &&
            tokens[statementStart + 1].kind == TokenKind::Identifier) {
            keywords.push_back("in");
        }
    }

    for (const std::string& keyword : keywords) {
        if (!matchesPrefix(keyword, prefix, true)) {
            continue;
        }
        addItem(items, keyword, keyword, CompletionKind::Keyword, "关键字",
                std::nullopt, 0, false, replace);
    }
}

void addLiteral(std::vector<CompletionItem>& items, std::string text,
                ValueType type, int dim, CompletionKind kind,
                std::string_view prefix, Span replace) {
    if (!matchesPrefix(text, prefix, true)) {
        return;
    }
    std::string detail = valueTypeLabel(type, dim);
    addItem(items, text, text, kind, std::move(detail), type, dim, false,
            replace);
}

void addLiteralItems(std::vector<CompletionItem>& items,
                     CompletionContext context,
                     const std::optional<command::ParamSpec>& expected,
                     std::string_view prefix, Span replace) {
    if (context == CompletionContext::ForIterable) {
        if (prefix.empty()) {
            addLiteral(items, "[]", ValueType::List, 0,
                       CompletionKind::Literal, prefix, replace);
            addLiteral(items, "\"\"", ValueType::String, 0,
                       CompletionKind::Literal, prefix, replace);
        }
        return;
    }

    if (!expected) {
        if (context == CompletionContext::Expression ||
            context == CompletionContext::ParenExpression ||
            context == CompletionContext::ListElement) {
            addLiteral(items, "true", ValueType::Bool, 0,
                       CompletionKind::Literal, prefix, replace);
            addLiteral(items, "false", ValueType::Bool, 0,
                       CompletionKind::Literal, prefix, replace);
        }
        return;
    }

    switch (expected->type) {
    case command::ParamType::Int:
        addLiteral(items, "0", ValueType::Int, 0, CompletionKind::Literal,
                   prefix, replace);
        break;
    case command::ParamType::Number:
        addLiteral(items, "0", ValueType::Int, 0, CompletionKind::Literal,
                   prefix, replace);
        addLiteral(items, "0.0", ValueType::Double, 0,
                   CompletionKind::Literal, prefix, replace);
        break;
    case command::ParamType::Bool:
        addLiteral(items, "true", ValueType::Bool, 0, CompletionKind::Literal,
                   prefix, replace);
        addLiteral(items, "false", ValueType::Bool, 0, CompletionKind::Literal,
                   prefix, replace);
        break;
    case command::ParamType::String:
        addLiteral(items, "\"\"", ValueType::String, 0,
                   CompletionKind::Literal, prefix, replace);
        break;
    case command::ParamType::List:
        addLiteral(items, "[]", ValueType::List, 0, CompletionKind::Literal,
                   prefix, replace);
        break;
    case command::ParamType::Point:
        if (prefix.empty()) {
            addItem(items, "(", "(", CompletionKind::Syntax,
                    "Point/分组表达式", ValueType::Point, expected->dim, false,
                    replace);
        }
        break;
    case command::ParamType::Any:
        break;
    }
}

void addMemberItems(std::vector<CompletionItem>& items,
                    const TypeInfo& receiver, std::string_view prefix,
                    Span replace) {
    if (!receiver.type) {
        return;
    }

    std::vector<std::string> members;
    if (*receiver.type == ValueType::Point) {
        members.push_back("x");
        members.push_back("y");
        if (receiver.dim != 2) {
            members.push_back("z");
        }
    } else if (*receiver.type == ValueType::List ||
               *receiver.type == ValueType::String) {
        members.push_back("length");
    }

    for (const std::string& member : members) {
        if (!matchesPrefix(member, prefix, false)) {
            continue;
        }
        const TypeInfo memberInfo = memberType(receiver, member);
        std::string detail = "成员 of " +
                             valueTypeLabel(*receiver.type, receiver.dim);
        addItem(items, member, member, CompletionKind::Member,
                std::move(detail), memberInfo.type, memberInfo.dim, false,
                replace);
    }
}

std::optional<command::ParamSpec> sessionParam(
    const command::NeedInput& input) {
    command::ParamSpec param;
    param.name = "session";
    param.type = input.type;
    param.dim = input.dim;
    param.hint = input.prompt;
    return param;
}

std::size_t callArgumentIndex(const std::vector<Token>& tokens,
                              std::size_t openIndex) {
    std::size_t depth = 0;
    std::size_t index = 0;
    for (std::size_t cursor = openIndex + 1; cursor < tokens.size();
         ++cursor) {
        const TokenKind kind = tokens[cursor].kind;
        if (isLeftDelimiter(kind)) {
            ++depth;
        } else if (isRightDelimiter(kind)) {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0 && kind == TokenKind::Comma) {
            ++index;
        }
    }
    return index;
}

bool isCallableToken(const Token& token, const core::Env& env,
                     const command::Registry& registry) {
    if (token.kind != TokenKind::Identifier) {
        return false;
    }
    if (const std::optional<CallableDescriptor> descriptor =
            findDescriptor(token.text, registry)) {
        return descriptor->signature.returnsValue;
    }
    if (const std::shared_ptr<core::Cell> cell = env.find(token.text)) {
        return cell->read().isCallable();
    }
    return false;
}

} // namespace

CompletionResult complete(const CompletionRequest& request) {
    CompletionResult emptyResult;
    if (request.cursor > request.source.text().size()) {
        return emptyResult;
    }

    const std::string prefixText(
        request.source.text().substr(0, request.cursor));
    core::Source prefixSource(prefixText);
    core::Lexer lexer(prefixSource);
    const core::LexResult lex = lexer.tokenize();
    if (lex.diagnostic) {
        return emptyResult;
    }

    std::vector<Token> tokens;
    tokens.reserve(lex.tokens.size());
    for (const Token& token : lex.tokens) {
        if (token.kind != TokenKind::EndOfFile) {
            tokens.push_back(token);
        }
    }

    PrefixInfo prefixInfo = prefixOf(tokens, prefixText, request.cursor);
    if (prefixInfo.tokenIndex) {
        core::Lexer fullLexer(request.source);
        const core::LexResult fullLex = fullLexer.tokenize();
        if (!fullLex.diagnostic) {
            for (const Token& token : fullLex.tokens) {
                if (!isIdentifierLike(token.kind)) {
                    continue;
                }
                if (token.offset <= request.cursor &&
                    request.cursor <= token.offset + token.length &&
                    token.offset ==
                        tokens[*prefixInfo.tokenIndex].offset) {
                    prefixInfo.replace.end = token.offset + token.length;
                    break;
                }
            }
        }
    }

    const std::size_t tokenEnd = lastTokenEnd(tokens);
    if (prefixText.find('#', tokenEnd) != std::string::npos) {
        return emptyResult;
    }
    if (!prefixInfo.tokenIndex.has_value() && !tokens.empty() &&
        tokens.back().offset + tokens.back().length == request.cursor &&
        isAtomicToken(tokens.back().kind)) {
        return emptyResult;
    }

    emptyResult.replaceSpan = prefixInfo.replace;
    const Structure structure = structureOf(tokens);
    const std::size_t statementStart = structure.statementStart;

    std::optional<std::size_t> dotIndex;
    if (prefixInfo.tokenIndex && *prefixInfo.tokenIndex > 0 &&
        tokens[*prefixInfo.tokenIndex - 1].kind == TokenKind::Dot) {
        dotIndex = *prefixInfo.tokenIndex - 1;
    } else if (!tokens.empty() &&
               tokens.back().kind == TokenKind::Dot &&
               tokens.back().offset + tokens.back().length ==
                   request.cursor) {
        dotIndex = tokens.size() - 1;
    }

    CompletionContext context = CompletionContext::None;
    std::optional<command::ParamSpec> expected;
    bool suppressCandidates = false;

    if (dotIndex) {
        context = CompletionContext::Member;
        const TypeInfo receiver = inferReceiverType(
            tokens, *dotIndex, request.env, request.registry);
        addMemberItems(emptyResult.items, receiver, prefixInfo.prefix,
                       prefixInfo.replace);
    } else if (!structure.delimiters.empty()) {
        const auto& open = structure.delimiters.back();
        if (open.first == TokenKind::LeftBracket) {
            context = CompletionContext::ListElement;
        } else {
            const std::size_t openIndex = open.second;
            const bool call = openIndex > statementStart &&
                              !tokens[openIndex].wsBefore &&
                              isCallableToken(tokens[openIndex - 1],
                                              request.env, request.registry);
            if (call) {
                context = CompletionContext::CallArgument;
                if (const std::optional<CallableDescriptor> descriptor =
                        findDescriptor(tokens[openIndex - 1].text,
                                       request.registry)) {
                    expected = paramAt(descriptor->signature,
                                       callArgumentIndex(tokens, openIndex));
                    if (!expected) {
                        suppressCandidates = true;
                    }
                }
            } else {
                context = CompletionContext::ParenExpression;
            }
        }
    } else {
        bool forIterable = false;
        if (statementStart < tokens.size() &&
            tokens[statementStart].kind == TokenKind::KwFor) {
            for (std::size_t index = statementStart + 1;
                 index < tokens.size(); ++index) {
                if (tokens[index].kind == TokenKind::KwIn) {
                    forIterable = true;
                    break;
                }
            }
        }

        if (forIterable) {
            context = CompletionContext::ForIterable;
        } else if (statementStart >= tokens.size()) {
            context = CompletionContext::StatementStart;
        } else {
            const Token& first = tokens[statementStart];
            const std::optional<CallableDescriptor> commandDescriptor =
                first.kind == TokenKind::Identifier
                    ? findDescriptor(first.text, request.registry)
                    : std::nullopt;

            bool hasAssignment = false;
            for (std::size_t index = statementStart; index < tokens.size();
                 ++index) {
                if (tokens[index].kind == TokenKind::Assign) {
                    hasAssignment = true;
                    break;
                }
            }

            if (first.kind == TokenKind::KwFor) {
                context = CompletionContext::StatementStart;
            } else if (commandDescriptor && !hasAssignment) {
                const bool whitespaceAfter =
                    hasWhitespaceBetween(prefixText,
                                         first.offset + first.length,
                                         request.cursor);
                if (!whitespaceAfter &&
                    tokens.size() == statementStart + 1) {
                    context = CompletionContext::StatementStart;
                } else if (!tokens.empty() &&
                           isExpressionContinuation(tokens.back().kind)) {
                    context = CompletionContext::Expression;
                } else if (tokens.size() > statementStart + 1 &&
                           !tokens[statementStart + 1].wsBefore) {
                    context = CompletionContext::ParenExpression;
                } else {
                    const std::vector<core::CommandWord> words =
                        core::splitCommandWords(tokens, statementStart + 1,
                                                tokens.size());
                    std::size_t argumentIndex = words.size();
                    if (prefixInfo.tokenIndex) {
                        argumentIndex =
                            words.empty() ? 0 : words.size() - 1;
                    }
                    context = CompletionContext::CommandArgument;
                    if (const std::optional<command::ParamSpec> param =
                            paramAt(commandDescriptor->signature,
                                    argumentIndex)) {
                        expected = param;
                    } else {
                        suppressCandidates = true;
                    }
                }
            } else if (tokens.size() == statementStart + 1 &&
                       prefixInfo.tokenIndex) {
                context = CompletionContext::StatementStart;
            } else {
                context = CompletionContext::Expression;
            }
        }
    }

    if (request.sessionInput && !expected) {
        if (context == CompletionContext::StatementStart ||
            context == CompletionContext::Expression ||
            context == CompletionContext::ParenExpression ||
            context == CompletionContext::ListElement) {
            context = CompletionContext::SessionArgument;
            expected = sessionParam(*request.sessionInput);
        }
    }

    if (suppressCandidates) {
        emptyResult.context = context;
        emptyResult.replaceSpan = prefixInfo.replace;
        return emptyResult;
    }

    const std::string_view prefix = prefixInfo.prefix;
    switch (context) {
    case CompletionContext::StatementStart:
        addVariableItems(emptyResult.items, request.env, prefix, std::nullopt,
                         false, prefixInfo.replace);
        addCallableItems(emptyResult.items, request.registry, prefix, context,
                         std::nullopt, prefixInfo.replace);
        addKeywordItems(emptyResult.items, tokens, statementStart, prefix,
                        prefixInfo.replace);
        break;
    case CompletionContext::Expression:
    case CompletionContext::ParenExpression:
    case CompletionContext::ListElement:
        addVariableItems(emptyResult.items, request.env, prefix, expected,
                         false, prefixInfo.replace);
        addCallableItems(emptyResult.items, request.registry, prefix, context,
                         expected, prefixInfo.replace);
        addLiteralItems(emptyResult.items, context, expected, prefix,
                        prefixInfo.replace);
        break;
    case CompletionContext::CommandArgument:
    case CompletionContext::CallArgument:
    case CompletionContext::SessionArgument:
        addVariableItems(emptyResult.items, request.env, prefix, expected,
                         false, prefixInfo.replace);
        addCallableItems(emptyResult.items, request.registry, prefix, context,
                         expected, prefixInfo.replace);
        addLiteralItems(emptyResult.items, context, expected, prefix,
                        prefixInfo.replace);
        break;
    case CompletionContext::ForIterable:
        addVariableItems(emptyResult.items, request.env, prefix, std::nullopt,
                         true, prefixInfo.replace);
        addLiteralItems(emptyResult.items, context, std::nullopt, prefix,
                        prefixInfo.replace);
        break;
    case CompletionContext::Member:
    case CompletionContext::None:
        break;
    }

    if (expected) {
        const std::string suffix = expectedSuffix(*expected);
        if (!suffix.empty()) {
            for (CompletionItem& item : emptyResult.items) {
                item.detail = item.detail.empty() ? suffix
                                                   : item.detail + " — " + suffix;
            }
        }
    }

    std::stable_sort(
        emptyResult.items.begin(), emptyResult.items.end(),
        [](const CompletionItem& left, const CompletionItem& right) {
            if (left.typeMismatch != right.typeMismatch) {
                return !left.typeMismatch;
            }
            if (left.label != right.label) {
                return left.label < right.label;
            }
            return static_cast<int>(left.kind) <
                   static_cast<int>(right.kind);
        });
    emptyResult.context = context;
    emptyResult.replaceSpan = prefixInfo.replace;
    return emptyResult;
}

} // namespace lush::completion

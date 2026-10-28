#pragma once

#include "lush/core/output.h"
#include "lush/core/value.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

// 名称目录与内建函数。
//
// NameCatalog 是校验/求值看到的“可调用名字”抽象；BuiltinCatalog 提供
// int/echo 等内建。命令宿主通过 command::Registry 扩展同一接口。
namespace lush::core {

// 命令调用位置信息；只在宿主调用期间有效。
struct CommandContext {
    Span nameSpan{};
    SourceId nameSource = kNoSource;
    std::vector<Span> argSpans;
    std::vector<SourceId> argSources;
};

// 注册 callable 的语言级形状。这里只描述 arity/交互/是否有值；具体
// 参数类型由调用者或命令绑定器检查。
struct CallableInfo {
    std::size_t required = 0;
    std::size_t maxFixed = 0;
    bool variadic = false;
    bool interactive = false;
    bool returnsValue = false;
};

// 校验和求值使用的名字目录接口。
class NameCatalog {
public:
    virtual ~NameCatalog() = default;

    // 返回 callable 的 arity/交互信息；未知名字返回 nullopt。
    virtual std::optional<CallableInfo> callableInfo(
        std::string_view name) const = 0;

    // 返回可作为值使用的 callable；不存在的名字返回空指针。
    virtual CallablePtr findCallable(std::string_view name) const = 0;

    // 以命令位置调用名字；R010 等宿主错误通过 Diagnostic 返回。
    virtual std::optional<Diagnostic> invokeCommand(
        std::string_view name, const std::vector<Value>& args, Output& output,
        const CommandContext& context) const = 0;

    // 命令与保留名的便捷判断，由 callableInfo 派生。
    bool isCommand(std::string_view name) const;
    bool isReserved(std::string_view name) const;
};

// 语言内建 callable 目录（int/double/.../echo/len）。
class BuiltinCatalog final : public NameCatalog {
public:
    std::optional<CallableInfo> callableInfo(
        std::string_view name) const override;
    CallablePtr findCallable(std::string_view name) const override;
    std::optional<Diagnostic> invokeCommand(
        std::string_view name, const std::vector<Value>& args, Output& output,
        const CommandContext& context) const override;
};

} // namespace lush::core

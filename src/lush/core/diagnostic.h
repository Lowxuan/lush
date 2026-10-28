#pragma once

#include "lush/core/source.h"

#include <string>
#include <string_view>

// 诊断码与诊断结构。
//
// 码段：L* 词法、P* 语法、S* 校验、R* 运行时；新增码必须同步
// docs/spec.md §11.1、errorCodeName 和至少一个测试。
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
    S007, // 0.4.0 起弃用；def/return 已由 Parser 支持
    S008,
    S009,
    S010,
    S011,
    S012,
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
    R014,
    R015,
};

std::string_view errorCodeName(ErrorCode code) noexcept;

// 一条可显示诊断；span 是原始物理文本位置。
struct Diagnostic {
    ErrorCode code = ErrorCode::L001;
    Span span{};
    std::string message;
    std::string help;
    // 可选的源身份，供宿主/会话诊断使用；core 诊断通常留 kNoSource，
    // 由 Driver 在显示时补当前 SourceId。
    SourceId source = kNoSource;
};

// 按 docs/spec.md §11.2 的格式渲染诊断。
std::string formatDiagnostic(const Source& source, const Diagnostic& diagnostic);

} // namespace lush::core

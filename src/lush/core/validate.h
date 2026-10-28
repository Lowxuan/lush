#pragma once

#include "lush/core/ast.h"
#include "lush/core/diagnostic.h"

#include <optional>

// 静态校验入口。
//
// 程序校验分两遍：先收集顶层函数签名，再按源码顺序校验名称、类型和
// return 形态。这样既支持前向函数引用，也保证报告源码中的第一个错误。
namespace lush::core {

class NameCatalog;
class Env;

struct ValidationOptions {
    // 仅当调用方已确认输入是 REPL 顶层单条命令，并允许进入交互补参时
    // 为 true；脚本和嵌套语句保持 false。
    bool allowInteractiveSession = false;

    // REPL 允许替换已有用户函数；脚本模式把重复定义视为 S011。
    bool allowFunctionRedefinition = false;

    // 可选的已有环境，用于检查变量/函数名冲突；不拥有 Env。
    const Env* env = nullptr;
};

struct ValidationResult {
    std::optional<Diagnostic> diagnostic;
};

// 校验整段程序；options.env 非空时同时检查已有变量和函数。
ValidationResult validate(const Program& program, const NameCatalog& names,
                          ValidationOptions options = {});

// 校验单个表达式（会话参数/补全等场景），不建立函数表。
ValidationResult validateExpression(const Expr& expression,
                                    const NameCatalog& names);

} // namespace lush::core

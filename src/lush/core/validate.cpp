#include "lush/core/validate.h"

#include "lush/core/builtins.h"
#include "lush/core/cell.h"
#include "lush/core/function_table.h"
#include "lush/core/text.h"

// 静态校验实现。
//
// 两遍结构：
//   1. collectFunctions 收集所有顶层 def 签名（不报错，保证前向引用）；
//   2. validateNode 按源码顺序校验，首个错误写入 Diagnostic 后停止。
// 这样既支持后定义函数，也满足“首个源码错误优先”的行为。

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

namespace lush::core {
namespace {

// 单次校验会话；不拥有 AST/Env/NameCatalog，所有结果保存在 error_。
class Validator {
public:
    Validator(const NameCatalog& names, ValidationOptions options)
        : names_(names), options_(options) {}

    // 校验整段程序：先收集函数签名，再按源码顺序校验。
    ValidationResult run(const Program& program) {
        collectFunctions(program);
        if (!error_) {
            validateNode(program);
        }
        return ValidationResult{std::move(error_)};
    }

    ValidationResult runExpression(const Expr& expression) {
        validateExpr(expression);
        return ValidationResult{std::move(error_)};
    }

private:
    void fail(ErrorCode code, Span span, std::string message) {
        if (error_) {
            return;
        }
        error_ = Diagnostic{code, span, std::move(message), {}};
    }

    // 变量/写入目标不能使用的名字：内建/命令、类型名、用户函数名。
    bool reserved(std::string_view name) const {
        return names_.isReserved(name) || isReservedTypeName(name) ||
               functions_.contains(name);
    }

    // 参数名额外禁止与任意用户函数同名，避免参数遮蔽函数值。
    bool reservedParam(std::string_view name) const {
        return names_.isReserved(name) || isReservedTypeName(name) ||
               functions_.contains(name);
    }

    const FunctionSignature* userFunction(std::string_view name) const {
        return functions_.find(name);
    }

    bool sameName(std::string_view left, std::string_view right) const {
        return asciiEqualsIgnoreCase(left, right);
    }

    // 检查 Env 中是否已有同名变量（不区分 ASCII 大小写）。
    bool hasVisibleVariable(std::string_view name) const {
        if (options_.env == nullptr) {
            return false;
        }
        for (const std::string& variable : options_.env->visibleNames()) {
            if (sameName(variable, name)) {
                return true;
            }
        }
        return false;
    }

    // 第一遍：复制 Env 中已有签名，并注册本程序的所有顶层 def。
    // 这里不报重复/保留名错误，错误留给 validateNode 按源码顺序报告。
    void collectFunctions(const Program& program) {
        if (options_.env != nullptr) {
            for (const FunctionSignature& existing :
                 options_.env->functions().signatures()) {
                functions_.insert(existing, true);
            }
        }

        for (const StmtPtr& statement : program.statements) {
            if (statement->kind() == NodeKind::Def) {
                functions_.insert(
                    static_cast<const Def&>(*statement).signature, true);
            }
        }
    }

    // 校验类型注解：未知类型 S011；void 只允许返回位置，不允许参数。
    void validateTypeAnnotation(const TypeAnnotation& type, bool parameter) {
        if (type.kind == TypeKind::Unknown) {
            fail(ErrorCode::S011, type.span, "未知类型 " + type.text);
            return;
        }
        if (parameter && type.kind == TypeKind::Void) {
            fail(ErrorCode::S011, type.span, "void 不能作为参数类型");
        }
    }

    // 校验函数签名内部规则：参数保留名/函数名冲突/重复、类型注解。
    // 函数名与已有变量/函数的冲突由 validateNode(Def) 在源码位置检查。
    void validateSignature(const FunctionSignature& signature) {
        if (error_) {
            return;
        }
        std::unordered_set<std::string> names;
        for (const FunctionParam& param : signature.params) {
            if (error_) {
                return;
            }
            if (reservedParam(param.name.text)) {
                fail(ErrorCode::S011, param.name.span,
                     "参数名不能使用保留名");
                return;
            }
            if (sameName(param.name.text, signature.name.text)) {
                fail(ErrorCode::S011, param.name.span,
                     "参数名不能和函数名相同");
                return;
            }
            if (!names.insert(param.name.text).second) {
                fail(ErrorCode::S011, param.name.span, "参数名重复");
                return;
            }
            if (param.type) {
                validateTypeAnnotation(*param.type, true);
            }
        }
        if (signature.returnType) {
            validateTypeAnnotation(*signature.returnType, false);
        }
    }

    const Name* rootName(const Expr& expression) const {
        switch (expression.kind()) {
        case NodeKind::Variable:
            return &static_cast<const Variable&>(expression).name;
        case NodeKind::Index:
            return rootName(*static_cast<const Index&>(expression).receiver);
        case NodeKind::Member:
            return rootName(*static_cast<const Member&>(expression).receiver);
        case NodeKind::Deref:
            return &static_cast<const Deref&>(expression).variable->name;
        default:
            return nullptr;
        }
    }

    void validateWriteTarget(const Expr& target) {
        if (error_) {
            return;
        }

        const Name* root = rootName(target);
        if (root != nullptr && reserved(root->text)) {
            fail(ErrorCode::S003, root->span, "保留名不能用作变量");
            return;
        }

        validateWriteTargetSubexpressions(target);
    }

    void validateWriteTargetSubexpressions(const Expr& target) {
        switch (target.kind()) {
        case NodeKind::Variable:
            return;
        case NodeKind::Deref:
            return;
        case NodeKind::Index: {
            const auto& index = static_cast<const Index&>(target);
            validateWriteTargetSubexpressions(*index.receiver);
            validateExpr(*index.index);
            return;
        }
        case NodeKind::Member:
            validateWriteTargetSubexpressions(
                *static_cast<const Member&>(target).receiver);
            return;
        default:
            validateExpr(target);
            return;
        }
    }

    void validateExpr(const Expr& expression) {
        if (error_) {
            return;
        }

        switch (expression.kind()) {
        case NodeKind::Int:
        case NodeKind::BoundaryInt:
        case NodeKind::Float:
        case NodeKind::String:
        case NodeKind::Bool:
            return;
        case NodeKind::Variable: {
            const auto& variable = static_cast<const Variable&>(expression);
            if (sameName(variable.name.text, "callable")) {
                return;
            }
            if (const FunctionSignature* function =
                    userFunction(variable.name.text)) {
                if (!function->returnsValue()) {
                    fail(ErrorCode::S012, variable.name.span,
                         "void 函数没有值形态");
                }
                return;
            }
            if (names_.isCommand(variable.name.text)) {
                fail(ErrorCode::S008, variable.name.span,
                     "命令没有值形态");
            }
            return;
        }
        case NodeKind::Unary: {
            validateExpr(*static_cast<const Unary&>(expression).operand);
            return;
        }
        case NodeKind::Binary: {
            const auto& binary = static_cast<const Binary&>(expression);
            validateExpr(*binary.left);
            validateExpr(*binary.right);
            return;
        }
        case NodeKind::Ternary: {
            const auto& ternary = static_cast<const Ternary&>(expression);
            validateExpr(*ternary.cond);
            validateExpr(*ternary.thenExpr);
            validateExpr(*ternary.elseExpr);
            return;
        }
        case NodeKind::Call: {
            const auto& call = static_cast<const Call&>(expression);
            if (call.callee->kind() == NodeKind::Variable) {
                const auto& callee = static_cast<const Variable&>(*call.callee);
                if (const FunctionSignature* function =
                        userFunction(callee.name.text)) {
                    if (!function->returnsValue()) {
                        fail(ErrorCode::S012, callee.name.span,
                             "void 函数不能用于表达式");
                    } else if (function->params.size() != call.args.size()) {
                        fail(ErrorCode::R002, call.lparenSpan,
                             "参数个数不符");
                    }
                }
            }
            validateExpr(*call.callee);
            for (const ExprPtr& argument : call.args) {
                validateExpr(*argument);
            }
            return;
        }
        case NodeKind::Index: {
            const auto& index = static_cast<const Index&>(expression);
            validateExpr(*index.receiver);
            validateExpr(*index.index);
            return;
        }
        case NodeKind::Member: {
            const auto& member = static_cast<const Member&>(expression);
            validateExpr(*member.receiver);
            return;
        }
        case NodeKind::IncDec: {
            const auto& incDec = static_cast<const IncDec&>(expression);
            validateWriteTarget(*incDec.target);
            return;
        }
        case NodeKind::Deref:
            // A dereference in read position is a variable read; absence of the
            // variable is a runtime R001, not S003.
            return;
        case NodeKind::Point: {
            const auto& point = static_cast<const Point&>(expression);
            for (const ExprPtr& coordinate : point.coordinates) {
                validateExpr(*coordinate);
            }
            return;
        }
        case NodeKind::List: {
            const auto& list = static_cast<const List&>(expression);
            for (const ExprPtr& element : list.elements) {
                validateExpr(*element);
            }
            return;
        }
        default:
            return;
        }
    }

    // 语句分派：循环/break、赋值目标、命令 arity、Def/Return 规则。
    // 出错后立即返回，后续调用因 error_ 已设置而短路。
    void validateNode(const Node& statement) {
        if (error_) {
            return;
        }

        switch (statement.kind()) {
        case NodeKind::Program: {
            const auto& program = static_cast<const Program&>(statement);
            for (const StmtPtr& child : program.statements) {
                validateNode(*child);
            }
            return;
        }
        case NodeKind::Block: {
            const auto& block = static_cast<const Block&>(statement);
            ++nestingDepth_;
            for (const StmtPtr& child : block.statements) {
                validateNode(*child);
            }
            --nestingDepth_;
            return;
        }
        case NodeKind::If: {
            const auto& ifStatement = static_cast<const If&>(statement);
            validateExpr(*ifStatement.cond);
            ++nestingDepth_;
            validateNode(*ifStatement.thenBranch);
            --nestingDepth_;
            if (ifStatement.elseBranch) {
                ++nestingDepth_;
                validateNode(*ifStatement.elseBranch);
                --nestingDepth_;
            }
            return;
        }
        case NodeKind::While: {
            const auto& whileStatement = static_cast<const While&>(statement);
            validateExpr(*whileStatement.cond);
            ++nestingDepth_;
            ++loopDepth_;
            validateNode(*whileStatement.body);
            --loopDepth_;
            --nestingDepth_;
            return;
        }
        case NodeKind::ForIn: {
            const auto& forIn = static_cast<const ForIn&>(statement);
            if (reserved(forIn.var.text)) {
                fail(ErrorCode::S003, forIn.var.span,
                     "保留名不能用作变量");
                return;
            }
            validateExpr(*forIn.iterable);
            ++nestingDepth_;
            ++loopDepth_;
            validateNode(*forIn.body);
            --loopDepth_;
            --nestingDepth_;
            return;
        }
        case NodeKind::For: {
            const auto& forStatement = static_cast<const For&>(statement);
            if (forStatement.init) {
                validateWriteTarget(*forStatement.init->target);
                validateExpr(*forStatement.init->value);
            }
            if (forStatement.cond) {
                validateExpr(*forStatement.cond);
            }
            if (forStatement.step) {
                validateExpr(*forStatement.step);
            }
            ++nestingDepth_;
            ++loopDepth_;
            validateNode(*forStatement.body);
            --loopDepth_;
            --nestingDepth_;
            return;
        }
        case NodeKind::Break:
        case NodeKind::Continue:
            if (loopDepth_ == 0) {
                fail(ErrorCode::S005, statement.span,
                     "break/continue 不在循环内");
            }
            return;
        case NodeKind::Def: {
            // 顶层限定；函数名冲突、签名和 body 都按源码顺序检查。
            const auto& def = static_cast<const Def&>(statement);
            if (nestingDepth_ != 0 || functionDepth_ != 0) {
                fail(ErrorCode::S011, def.signature.name.span,
                     "def 只允许出现在程序顶层");
                return;
            }
            const bool existing =
                options_.env != nullptr &&
                options_.env->functions().contains(def.signature.name.text);
            if (existing && !options_.allowFunctionRedefinition) {
                fail(ErrorCode::S011, def.signature.name.span,
                     "用户函数已存在");
                return;
            }
            if (hasVisibleVariable(def.signature.name.text)) {
                fail(ErrorCode::S003, def.signature.name.span,
                     "函数名与已有变量冲突");
                return;
            }
            if (names_.isReserved(def.signature.name.text) ||
                isReservedTypeName(def.signature.name.text)) {
                fail(ErrorCode::S011, def.signature.name.span,
                     "函数名不能使用保留名");
                return;
            }
            const std::string normalizedName =
                asciiLower(def.signature.name.text);
            if (!seenFunctions_.insert(normalizedName).second &&
                !options_.allowFunctionRedefinition) {
                fail(ErrorCode::S011, def.signature.name.span,
                     "同名用户函数重复定义");
                return;
            }
            validateSignature(def.signature);
            if (error_) {
                return;
            }
            const std::optional<TypeAnnotation> previousReturn =
                currentReturnType_;
            const int previousFunctionDepth = functionDepth_;
            const int previousNestingDepth = nestingDepth_;
            const int previousLoopDepth = loopDepth_;
            currentReturnType_ = def.signature.returnType;
            ++functionDepth_;
            nestingDepth_ = 1;
            loopDepth_ = 0;
            validateNode(*def.body);
            currentReturnType_ = previousReturn;
            functionDepth_ = previousFunctionDepth;
            nestingDepth_ = previousNestingDepth;
            loopDepth_ = previousLoopDepth;
            return;
        }
        case NodeKind::Return: {
            // S010：函数外；S012：void 带值或值函数 bare return。
            const auto& returnStatement = static_cast<const Return&>(statement);
            if (functionDepth_ == 0) {
                fail(ErrorCode::S010, returnStatement.span,
                     "return 只能出现在函数体内");
                return;
            }
            const bool valueFunction =
                currentReturnType_.has_value() &&
                currentReturnType_->kind != TypeKind::Void;
            if (returnStatement.value && !valueFunction) {
                fail(ErrorCode::S012, returnStatement.span,
                     "void 函数不能返回值");
                return;
            }
            if (!returnStatement.value && valueFunction) {
                fail(ErrorCode::S012, returnStatement.span,
                     "值函数必须返回表达式");
                return;
            }
            if (returnStatement.value) {
                validateExpr(*returnStatement.value);
            }
            return;
        }
        case NodeKind::Assign: {
            const auto& assign = static_cast<const Assign&>(statement);
            validateWriteTarget(*assign.target);
            validateExpr(*assign.value);
            return;
        }
        case NodeKind::RefAssign: {
            const auto& refAssign = static_cast<const RefAssign&>(statement);
            if (reserved(refAssign.target.text)) {
                fail(ErrorCode::S003, refAssign.target.span,
                     "保留名不能用作变量");
            }
            return;
        }
        case NodeKind::Command: {
            const auto& command = static_cast<const Command&>(statement);
            if (const FunctionSignature* function =
                    userFunction(command.name.text)) {
                if (command.args.size() != function->params.size()) {
                    fail(ErrorCode::S002, command.name.span,
                         "参数个数不符");
                    return;
                }
                for (const ExprPtr& argument : command.args) {
                    validateExpr(*argument);
                }
                return;
            }
            const std::optional<CallableInfo> info =
                names_.callableInfo(command.name.text);
            if (!info) {
                fail(ErrorCode::S001, command.name.span,
                     "未知命令 " + command.name.text);
                return;
            }

            const std::size_t argc = command.args.size();
            if (argc < info->required) {
                if (info->interactive) {
                    if (!options_.allowInteractiveSession) {
                        fail(ErrorCode::S006, command.name.span,
                             "交互命令必须在 REPL 中作为顶层单条语句运行，"
                             "或提供全部参数");
                        return;
                    }
                } else {
                    fail(ErrorCode::S002, command.name.span,
                         "参数个数不符");
                    return;
                }
            } else if (!info->variadic && argc > info->maxFixed) {
                fail(ErrorCode::S002, command.name.span, "参数个数不符");
                return;
            }

            for (const ExprPtr& argument : command.args) {
                validateExpr(*argument);
            }
            return;
        }
        case NodeKind::EmptyStmt:
            return;
        default:
            return;
        }
    }

    const NameCatalog& names_;
    ValidationOptions options_;
    std::optional<Diagnostic> error_;
    FunctionTable functions_;
    // 本程序已遇到的函数名（小写），用于按源码顺序检测重复定义。
    std::unordered_set<std::string> seenFunctions_;
    std::optional<TypeAnnotation> currentReturnType_;
    int loopDepth_ = 0;
    int functionDepth_ = 0;
    int nestingDepth_ = 0;
};

} // namespace

ValidationResult validate(const Program& program, const NameCatalog& names,
                          ValidationOptions options) {
    Validator validator(names, options);
    return validator.run(program);
}

ValidationResult validateExpression(const Expr& expression,
                                    const NameCatalog& names) {
    Validator validator(names, ValidationOptions{});
    return validator.runExpression(expression);
}

} // namespace lush::core

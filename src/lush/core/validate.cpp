#include "lush/core/validate.h"

#include "lush/core/builtins.h"

#include <string>
#include <string_view>

namespace lush::core {
namespace {

class Validator {
public:
    explicit Validator(const NameCatalog& names) : names_(names) {}

    ValidationResult run(const Program& program) {
        validateNode(program);
        return ValidationResult{std::move(error_)};
    }

private:
    void fail(ErrorCode code, Span span, std::string message) {
        if (error_) {
            return;
        }
        error_ = Diagnostic{code, span, std::move(message), {}};
    }

    bool reserved(std::string_view name) const {
        return names_.isReserved(name);
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
            for (const StmtPtr& child : block.statements) {
                validateNode(*child);
            }
            return;
        }
        case NodeKind::If: {
            const auto& ifStatement = static_cast<const If&>(statement);
            validateExpr(*ifStatement.cond);
            validateNode(*ifStatement.thenBranch);
            if (ifStatement.elseBranch) {
                validateNode(*ifStatement.elseBranch);
            }
            return;
        }
        case NodeKind::While: {
            const auto& whileStatement = static_cast<const While&>(statement);
            validateExpr(*whileStatement.cond);
            ++loopDepth_;
            validateNode(*whileStatement.body);
            --loopDepth_;
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
            ++loopDepth_;
            validateNode(*forIn.body);
            --loopDepth_;
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
            ++loopDepth_;
            validateNode(*forStatement.body);
            --loopDepth_;
            return;
        }
        case NodeKind::Break:
        case NodeKind::Continue:
            if (loopDepth_ == 0) {
                fail(ErrorCode::S005, statement.span,
                     "break/continue 不在循环内");
            }
            return;
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
            if (!names_.isCommand(command.name.text)) {
                fail(ErrorCode::S001, command.name.span,
                     "未知命令 " + command.name.text);
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
    std::optional<Diagnostic> error_;
    int loopDepth_ = 0;
};

} // namespace

ValidationResult validate(const Program& program, const NameCatalog& names) {
    Validator validator(names);
    return validator.run(program);
}

} // namespace lush::core

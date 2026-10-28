#include "lush/core/eval.h"

#include "lush/core/builtins.h"
#include "lush/core/checked.h"
#include "lush/core/utf8.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lush::core {
namespace {

enum class Flow {
    Normal,
    Break,
    Continue,
};

struct EvalExprResult {
    std::optional<Value> value;
    std::optional<Diagnostic> diagnostic;

    bool ok() const noexcept {
        return value.has_value();
    }
};

struct EvalFlowResult {
    Flow flow = Flow::Normal;
    std::optional<Diagnostic> diagnostic;

    bool ok() const noexcept {
        return !diagnostic.has_value();
    }
};

enum class StepKind {
    ListIndex,
    PointIndex,
    PointX,
    PointY,
    PointZ,
};

struct Step {
    StepKind kind = StepKind::ListIndex;
    std::size_t index = 0;
};

struct LValue {
    Span span{};
    bool simpleVariable = false;
    bool createOnWrite = false;
    std::shared_ptr<Cell> binding;
    std::string bindingName;
    std::shared_ptr<Cell> storage;
    std::vector<Step> path;
};

struct TargetResult {
    std::optional<LValue> lvalue;
    std::optional<Diagnostic> diagnostic;

    bool ok() const noexcept {
        return lvalue.has_value();
    }
};

struct PendingPostfix {
    LValue target;
    std::int64_t delta = 0;
    Span opSpan{};
};

Diagnostic makeDiagnostic(ErrorCode code, Span span, std::string message,
                          std::string help = {}) {
    return Diagnostic{code, span, std::move(message), std::move(help)};
}

EvalExprResult makeValue(Value value) {
    EvalExprResult result;
    result.value = std::move(value);
    return result;
}

EvalExprResult makeExprError(ErrorCode code, Span span, std::string message,
                             std::string help = {}) {
    EvalExprResult result;
    result.diagnostic =
        makeDiagnostic(code, span, std::move(message), std::move(help));
    return result;
}

EvalFlowResult makeFlow(Flow flow = Flow::Normal) {
    EvalFlowResult result;
    result.flow = flow;
    return result;
}

EvalFlowResult makeFlowError(Diagnostic diagnostic) {
    EvalFlowResult result;
    result.diagnostic = std::move(diagnostic);
    return result;
}

std::string actualTypeName(const Value& value) {
    if (value.isCallable() || value.isTypeToken()) {
        return "callable";
    }
    return std::string(valueTypeName(value.type()));
}

bool equalsIgnoreCaseName(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool isNumeric(const Value& value) {
    return value.isInt() || value.isDouble();
}

bool asDouble(const Value& value, double& out) {
    if (value.isInt()) {
        out = static_cast<double>(value.asInt());
        return true;
    }
    if (value.isDouble()) {
        out = value.asDouble();
        return true;
    }
    return false;
}

bool isFinite(double value) {
    return std::isfinite(value);
}

bool pointIsFinite(const PointValue& point) {
    for (int index = 0; index < point.dim; ++index) {
        if (!isFinite(point[index])) {
            return false;
        }
    }
    return true;
}

bool compareEqualValues(const Value& left, const Value& right, bool& equal) {
    if (left.isInt() && right.isInt()) {
        equal = left.asInt() == right.asInt();
        return true;
    }
    if (left.isDouble() && right.isDouble()) {
        equal = left.asDouble() == right.asDouble();
        return true;
    }
    if (left.isInt() && right.isDouble()) {
        equal = static_cast<double>(left.asInt()) == right.asDouble();
        return true;
    }
    if (left.isDouble() && right.isInt()) {
        equal = left.asDouble() == static_cast<double>(right.asInt());
        return true;
    }
    if (left.isBool() && right.isBool()) {
        equal = left.asBool() == right.asBool();
        return true;
    }
    if (left.isString() && right.isString()) {
        equal = left.asString() == right.asString();
        return true;
    }
    if (left.isList() && right.isList()) {
        const auto& leftItems = left.asList().items();
        const auto& rightItems = right.asList().items();
        if (leftItems.size() != rightItems.size()) {
            equal = false;
            return true;
        }
        for (std::size_t index = 0; index < leftItems.size(); ++index) {
            bool elementEqual = false;
            if (!compareEqualValues(leftItems[index], rightItems[index],
                                    elementEqual)) {
                return false;
            }
            if (!elementEqual) {
                equal = false;
                return true;
            }
        }
        equal = true;
        return true;
    }
    if (left.isPoint() && right.isPoint()) {
        const PointValue& leftPoint = left.asPoint();
        const PointValue& rightPoint = right.asPoint();
        if (leftPoint.dim != rightPoint.dim) {
            return false;
        }
        for (int index = 0; index < leftPoint.dim; ++index) {
            if (leftPoint[index] != rightPoint[index]) {
                equal = false;
                return true;
            }
        }
        equal = true;
        return true;
    }
    if (left.isCallable() && right.isCallable()) {
        equal = left.asCallable() == right.asCallable();
        return true;
    }
    if (left.isTypeToken() && right.isTypeToken()) {
        equal = left.asTypeToken() == right.asTypeToken();
        return true;
    }
    if ((left.isTypeToken() && right.isCallable()) ||
        (left.isCallable() && right.isTypeToken())) {
        equal = false;
        return true;
    }
    return false;
}

bool compareOrderValues(const Value& left, const Value& right, int& comparison) {
    if (left.isInt() && right.isInt()) {
        comparison = left.asInt() < right.asInt()
                         ? -1
                         : (left.asInt() > right.asInt() ? 1 : 0);
        return true;
    }
    if (isNumeric(left) && isNumeric(right)) {
        double leftValue = 0.0;
        double rightValue = 0.0;
        asDouble(left, leftValue);
        asDouble(right, rightValue);
        comparison = leftValue < rightValue
                         ? -1
                         : (leftValue > rightValue ? 1 : 0);
        return true;
    }
    if (left.isString() && right.isString()) {
        const int result = left.asString().compare(right.asString());
        comparison = result < 0 ? -1 : (result > 0 ? 1 : 0);
        return true;
    }
    return false;
}

class Evaluator {
public:
    explicit Evaluator(EvalContext& context) : context_(context) {}

    EvalExprResult evaluateExpression(const Expr& expression) {
        return evalExpr(expression);
    }

    EvalFlowResult evalNode(const Node& node) {
        switch (node.kind()) {
        case NodeKind::Program: {
            const auto& program = static_cast<const Program&>(node);
            for (const StmtPtr& statement : program.statements) {
                EvalFlowResult result = evalNode(*statement);
                if (!result.ok() || result.flow != Flow::Normal) {
                    return result;
                }
            }
            return makeFlow();
        }
        case NodeKind::Block: {
            const auto& block = static_cast<const Block&>(node);
            [[maybe_unused]] auto scope = context_.env.pushScope();
            for (const StmtPtr& statement : block.statements) {
                EvalFlowResult result = evalNode(*statement);
                if (!result.ok() || result.flow != Flow::Normal) {
                    return result;
                }
            }
            return makeFlow();
        }
        case NodeKind::If: {
            const auto& ifStatement = static_cast<const If&>(node);
            EvalExprResult cond = evalExpr(*ifStatement.cond);
            if (!cond.ok()) {
                return makeFlowError(std::move(*cond.diagnostic));
            }
            if (!cond.value->isBool()) {
                return makeFlowError(makeDiagnostic(
                    ErrorCode::R002, ifStatement.cond->span,
                    "条件必须是 bool，实际为 " + actualTypeName(*cond.value)));
            }
            if (cond.value->asBool()) {
                return evalNode(*ifStatement.thenBranch);
            }
            if (ifStatement.elseBranch) {
                return evalNode(*ifStatement.elseBranch);
            }
            return makeFlow();
        }
        case NodeKind::While: {
            const auto& whileStatement = static_cast<const While&>(node);
            while (true) {
                EvalExprResult cond = evalExpr(*whileStatement.cond);
                if (!cond.ok()) {
                    return makeFlowError(std::move(*cond.diagnostic));
                }
                if (!cond.value->isBool()) {
                    return makeFlowError(makeDiagnostic(
                        ErrorCode::R002, whileStatement.cond->span,
                        "条件必须是 bool，实际为 " +
                            actualTypeName(*cond.value)));
                }
                if (!cond.value->asBool()) {
                    break;
                }

                EvalFlowResult body = evalNode(*whileStatement.body);
                if (!body.ok()) {
                    return body;
                }
                if (body.flow == Flow::Break) {
                    break;
                }
            }
            return makeFlow();
        }
        case NodeKind::For: {
            const auto& forStatement = static_cast<const For&>(node);
            std::optional<Value> initValue;
            std::string initName;

            if (forStatement.init) {
                EvalExprResult value = evalExpr(*forStatement.init->value);
                if (!value.ok()) {
                    return makeFlowError(std::move(*value.diagnostic));
                }
                if (forStatement.init->target->kind() != NodeKind::Variable) {
                    return makeFlowError(makeDiagnostic(
                        ErrorCode::R002, forStatement.init->target->span,
                        "for init 目标必须是变量"));
                }
                initName = static_cast<const Variable&>(
                               *forStatement.init->target)
                               .name.text;
                initValue = std::move(value.value);
            }

            [[maybe_unused]] auto scope = context_.env.pushScope();
            if (initValue) {
                context_.env.create(initName, std::move(*initValue));
            }

            while (true) {
                if (forStatement.cond) {
                    EvalExprResult cond = evalExpr(*forStatement.cond);
                    if (!cond.ok()) {
                        return makeFlowError(std::move(*cond.diagnostic));
                    }
                    if (!cond.value->isBool()) {
                        return makeFlowError(makeDiagnostic(
                            ErrorCode::R002, forStatement.cond->span,
                            "条件必须是 bool，实际为 " +
                                actualTypeName(*cond.value)));
                    }
                    if (!cond.value->asBool()) {
                        break;
                    }
                }

                EvalFlowResult body = evalNode(*forStatement.body);
                if (!body.ok()) {
                    return body;
                }
                if (body.flow == Flow::Break) {
                    break;
                }

                if (forStatement.step) {
                    EvalExprResult step = evalExpr(*forStatement.step);
                    if (!step.ok()) {
                        return makeFlowError(std::move(*step.diagnostic));
                    }
                }
            }
            return makeFlow();
        }
        case NodeKind::ForIn: {
            const auto& forIn = static_cast<const ForIn&>(node);
            EvalExprResult iterable = evalExpr(*forIn.iterable);
            if (!iterable.ok()) {
                return makeFlowError(std::move(*iterable.diagnostic));
            }

            std::vector<Value> elements;
            if (iterable.value->isList()) {
                const auto& items = iterable.value->asList().items();
                elements.reserve(items.size());
                for (const Value& item : items) {
                    elements.push_back(item);
                }
            } else if (iterable.value->isString()) {
                const std::string& text = iterable.value->asString();
                const std::size_t length = utf8Length(text);
                elements.reserve(length);
                for (std::size_t index = 0; index < length; ++index) {
                    const std::optional<std::string> codePoint =
                        utf8At(text, index);
                    if (!codePoint) {
                        break;
                    }
                    elements.push_back(Value::makeString(*codePoint));
                }
            } else {
                return makeFlowError(makeDiagnostic(
                    ErrorCode::R002, forIn.iterable->span,
                    "for-in 只能迭代 list 或 string，实际为 " +
                        actualTypeName(*iterable.value)));
            }

            [[maybe_unused]] auto scope = context_.env.pushScope();
            for (const Value& element : elements) {
                auto cell = Cell::makeValue(element);
                context_.env.setCurrent(forIn.var.text, cell);

                EvalFlowResult body = evalNode(*forIn.body);
                if (!body.ok()) {
                    return body;
                }
                if (body.flow == Flow::Break) {
                    break;
                }
            }
            return makeFlow();
        }
        case NodeKind::Break:
            return makeFlow(Flow::Break);
        case NodeKind::Continue:
            return makeFlow(Flow::Continue);
        case NodeKind::Assign:
            return evalAssign(static_cast<const Assign&>(node));
        case NodeKind::RefAssign:
            return evalRefAssign(static_cast<const RefAssign&>(node));
        case NodeKind::Command:
            return evalCommand(static_cast<const Command&>(node));
        case NodeKind::Def:
            // M8a installs signatures only; execution is M8b.
            context_.env.functions().insert(
                static_cast<const Def&>(node).signature, true);
            return makeFlow();
        case NodeKind::Return:
            return makeFlowError(makeDiagnostic(
                ErrorCode::S010, node.span, "return 只能出现在函数体内"));
        case NodeKind::EmptyStmt:
            return makeFlow();
        default:
            return makeFlowError(makeDiagnostic(
                ErrorCode::R002, node.span, "无法执行的节点"));
        }
    }

private:
    EvalExprResult evalExpr(const Expr& expression) {
        switch (expression.kind()) {
        case NodeKind::Int:
            return makeValue(
                Value::makeInt(static_cast<const Int&>(expression).value));
        case NodeKind::BoundaryInt:
            return makeExprError(
                ErrorCode::L004,
                static_cast<const BoundaryInt&>(expression).literalSpan,
                "整数越界");
        case NodeKind::Float:
            return makeValue(Value::makeDouble(
                static_cast<const Float&>(expression).value));
        case NodeKind::String:
            return makeValue(Value::makeString(
                static_cast<const String&>(expression).value));
        case NodeKind::Bool:
            return makeValue(Value::makeBool(
                static_cast<const Bool&>(expression).value));
        case NodeKind::Variable:
            return evalVariable(static_cast<const Variable&>(expression));
        case NodeKind::Unary:
            return evalUnary(static_cast<const Unary&>(expression));
        case NodeKind::Binary:
            return evalBinary(static_cast<const Binary&>(expression));
        case NodeKind::Ternary:
            return evalTernary(static_cast<const Ternary&>(expression));
        case NodeKind::Call:
            return evalCall(static_cast<const Call&>(expression));
        case NodeKind::Index:
            return evalIndex(static_cast<const Index&>(expression));
        case NodeKind::Member:
            return evalMember(static_cast<const Member&>(expression));
        case NodeKind::IncDec:
            return evalIncDec(static_cast<const IncDec&>(expression));
        case NodeKind::Deref:
            return evalDeref(static_cast<const Deref&>(expression));
        case NodeKind::Point:
            return evalPoint(static_cast<const Point&>(expression));
        case NodeKind::List:
            return evalList(static_cast<const List&>(expression));
        default:
            return makeExprError(ErrorCode::R002, expression.span,
                                 "无法求值的表达式");
        }
    }

    EvalExprResult evalVariable(const Variable& variable) {
        if (equalsIgnoreCaseName(variable.name.text, "callable")) {
            return makeValue(Value::makeTypeToken());
        }

        auto cell = context_.env.find(variable.name.text);
        if (cell) {
            return makeValue(cell->readCopy());
        }

        CallablePtr callable = context_.names.findCallable(variable.name.text);
        if (callable) {
            return makeValue(Value::makeCallable(std::move(callable)));
        }

        if (context_.names.isCommand(variable.name.text)) {
            return makeExprError(ErrorCode::S008, variable.name.span,
                                 "命令没有值形态");
        }

        return makeExprError(ErrorCode::R001, variable.name.span,
                             "未定义变量 " + variable.name.text);
    }

    EvalExprResult evalUnary(const Unary& unary) {
        if (unary.op == UnaryOp::Negate &&
            unary.operand->kind() == NodeKind::BoundaryInt) {
            return makeValue(Value::makeInt(
                std::numeric_limits<std::int64_t>::min()));
        }

        EvalExprResult operand = evalExpr(*unary.operand);
        if (!operand.ok()) {
            return operand;
        }

        if (unary.op == UnaryOp::Not) {
            if (!operand.value->isBool()) {
                return makeExprError(
                    ErrorCode::R002, unary.opSpan,
                    "! 的操作数必须是 bool，实际为 " +
                        actualTypeName(*operand.value));
            }
            return makeValue(Value::makeBool(!operand.value->asBool()));
        }

        switch (operand.value->type()) {
        case ValueType::Int: {
            std::int64_t result = 0;
            if (!checkedNegate(operand.value->asInt(), result)) {
                return makeExprError(ErrorCode::R005, unary.opSpan,
                                     "整数溢出");
            }
            return makeValue(Value::makeInt(result));
        }
        case ValueType::Double: {
            const double result = -operand.value->asDouble();
            if (!isFinite(result)) {
                return makeExprError(ErrorCode::R009, unary.opSpan,
                                     "浮点溢出");
            }
            return makeValue(Value::makeDouble(result));
        }
        case ValueType::Point: {
            PointValue result = operand.value->asPoint();
            for (int index = 0; index < result.dim; ++index) {
                result[index] = -result[index];
            }
            if (!pointIsFinite(result)) {
                return makeExprError(ErrorCode::R009, unary.opSpan,
                                     "浮点溢出");
            }
            return makeValue(Value::makePoint(result));
        }
        case ValueType::Bool:
        case ValueType::String:
        case ValueType::List:
        case ValueType::Callable:
        case ValueType::TypeToken:
            return makeExprError(
                ErrorCode::R002, unary.opSpan,
                "一元 - 不支持 " + actualTypeName(*operand.value));
        }
        return makeExprError(ErrorCode::R002, unary.opSpan,
                             "一元 - 不支持该类型");
    }

    EvalExprResult evalBinary(const Binary& binary) {
        EvalExprResult left = evalExpr(*binary.left);
        if (!left.ok()) {
            return left;
        }

        if (binary.op == BinaryOp::LogicalAnd ||
            binary.op == BinaryOp::LogicalOr) {
            if (!left.value->isBool()) {
                return makeExprError(
                    ErrorCode::R002, binary.opSpan,
                    "逻辑运算的操作数必须是 bool，实际为 " +
                        actualTypeName(*left.value));
            }

            if (binary.op == BinaryOp::LogicalAnd &&
                !left.value->asBool()) {
                return makeValue(Value::makeBool(false));
            }
            if (binary.op == BinaryOp::LogicalOr &&
                left.value->asBool()) {
                return makeValue(Value::makeBool(true));
            }

            EvalExprResult right = evalExpr(*binary.right);
            if (!right.ok()) {
                return right;
            }
            if (!right.value->isBool()) {
                return makeExprError(
                    ErrorCode::R002, binary.opSpan,
                    "逻辑运算的操作数必须是 bool，实际为 " +
                        actualTypeName(*right.value));
            }
            return makeValue(Value::makeBool(right.value->asBool()));
        }

        EvalExprResult right = evalExpr(*binary.right);
        if (!right.ok()) {
            return right;
        }

        return applyBinary(binary.op, *left.value, *right.value,
                           binary.opSpan);
    }

    EvalExprResult applyBinary(BinaryOp op, const Value& left,
                               const Value& right, Span opSpan) {
        switch (op) {
        case BinaryOp::Add:
            if (left.isInt() && right.isInt()) {
                std::int64_t result = 0;
                if (!checkedAdd(left.asInt(), right.asInt(), result)) {
                    return makeExprError(ErrorCode::R005, opSpan,
                                         "整数溢出");
                }
                return makeValue(Value::makeInt(result));
            }
            if (isNumeric(left) && isNumeric(right)) {
                double leftValue = 0.0;
                double rightValue = 0.0;
                asDouble(left, leftValue);
                asDouble(right, rightValue);
                const double result = leftValue + rightValue;
                if (!isFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makeDouble(result));
            }
            if (left.isString() && right.isString()) {
                return makeValue(Value::makeString(
                    left.asString() + right.asString()));
            }
            if (left.isPoint() && right.isPoint()) {
                const PointValue& leftPoint = left.asPoint();
                const PointValue& rightPoint = right.asPoint();
                if (leftPoint.dim != rightPoint.dim) {
                    return makeExprError(ErrorCode::R002, opSpan,
                                         "Point 维度不一致");
                }
                PointValue result = leftPoint;
                for (int index = 0; index < result.dim; ++index) {
                    result[index] += rightPoint[index];
                }
                if (!pointIsFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makePoint(result));
            }
            return makeExprError(ErrorCode::R002, opSpan,
                                 "类型不匹配：无法相加 " +
                                     actualTypeName(left) + " 与 " +
                                     actualTypeName(right));
        case BinaryOp::Subtract:
            if (left.isInt() && right.isInt()) {
                std::int64_t result = 0;
                if (!checkedSub(left.asInt(), right.asInt(), result)) {
                    return makeExprError(ErrorCode::R005, opSpan,
                                         "整数溢出");
                }
                return makeValue(Value::makeInt(result));
            }
            if (isNumeric(left) && isNumeric(right)) {
                double leftValue = 0.0;
                double rightValue = 0.0;
                asDouble(left, leftValue);
                asDouble(right, rightValue);
                const double result = leftValue - rightValue;
                if (!isFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makeDouble(result));
            }
            if (left.isPoint() && right.isPoint()) {
                const PointValue& leftPoint = left.asPoint();
                const PointValue& rightPoint = right.asPoint();
                if (leftPoint.dim != rightPoint.dim) {
                    return makeExprError(ErrorCode::R002, opSpan,
                                         "Point 维度不一致");
                }
                PointValue result = leftPoint;
                for (int index = 0; index < result.dim; ++index) {
                    result[index] -= rightPoint[index];
                }
                if (!pointIsFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makePoint(result));
            }
            return makeExprError(ErrorCode::R002, opSpan,
                                 "类型不匹配：无法相减 " +
                                     actualTypeName(left) + " 与 " +
                                     actualTypeName(right));
        case BinaryOp::Multiply:
            if (left.isInt() && right.isInt()) {
                std::int64_t result = 0;
                if (!checkedMul(left.asInt(), right.asInt(), result)) {
                    return makeExprError(ErrorCode::R005, opSpan,
                                         "整数溢出");
                }
                return makeValue(Value::makeInt(result));
            }
            if (isNumeric(left) && isNumeric(right)) {
                double leftValue = 0.0;
                double rightValue = 0.0;
                asDouble(left, leftValue);
                asDouble(right, rightValue);
                const double result = leftValue * rightValue;
                if (!isFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makeDouble(result));
            }
            if (left.isPoint() && isNumeric(right)) {
                double scale = 0.0;
                asDouble(right, scale);
                PointValue result = left.asPoint();
                for (int index = 0; index < result.dim; ++index) {
                    result[index] *= scale;
                }
                if (!pointIsFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makePoint(result));
            }
            if (isNumeric(left) && right.isPoint()) {
                double scale = 0.0;
                asDouble(left, scale);
                PointValue result = right.asPoint();
                for (int index = 0; index < result.dim; ++index) {
                    result[index] *= scale;
                }
                if (!pointIsFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makePoint(result));
            }
            return makeExprError(ErrorCode::R002, opSpan,
                                 "类型不匹配：无法相乘 " +
                                     actualTypeName(left) + " 与 " +
                                     actualTypeName(right));
        case BinaryOp::Divide:
            if (left.isInt() && right.isInt()) {
                if (right.asInt() == 0) {
                    return makeExprError(ErrorCode::R004, opSpan, "除零");
                }
                std::int64_t result = 0;
                if (!checkedDiv(left.asInt(), right.asInt(), result)) {
                    return makeExprError(ErrorCode::R005, opSpan,
                                         "整数溢出");
                }
                return makeValue(Value::makeInt(result));
            }
            if (isNumeric(left) && isNumeric(right)) {
                double leftValue = 0.0;
                double rightValue = 0.0;
                asDouble(left, leftValue);
                asDouble(right, rightValue);
                if (rightValue == 0.0) {
                    return makeExprError(ErrorCode::R004, opSpan, "除零");
                }
                const double result = leftValue / rightValue;
                if (!isFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makeDouble(result));
            }
            if (left.isPoint() && isNumeric(right)) {
                double divisor = 0.0;
                asDouble(right, divisor);
                if (divisor == 0.0) {
                    return makeExprError(ErrorCode::R004, opSpan, "除零");
                }
                PointValue result = left.asPoint();
                for (int index = 0; index < result.dim; ++index) {
                    result[index] /= divisor;
                }
                if (!pointIsFinite(result)) {
                    return makeExprError(ErrorCode::R009, opSpan,
                                         "浮点溢出");
                }
                return makeValue(Value::makePoint(result));
            }
            return makeExprError(ErrorCode::R002, opSpan,
                                 "类型不匹配：无法相除 " +
                                     actualTypeName(left) + " 与 " +
                                     actualTypeName(right));
        case BinaryOp::Modulo:
            if (!left.isInt() || !right.isInt()) {
                return makeExprError(ErrorCode::R002, opSpan,
                                     "% 的操作数必须是 int");
            }
            if (right.asInt() == 0) {
                return makeExprError(ErrorCode::R004, opSpan, "除零");
            }
            {
                std::int64_t result = 0;
                if (!checkedMod(left.asInt(), right.asInt(), result)) {
                    return makeExprError(ErrorCode::R005, opSpan,
                                         "整数溢出");
                }
                return makeValue(Value::makeInt(result));
            }
        case BinaryOp::Equal:
        case BinaryOp::NotEqual: {
            bool equal = false;
            if (!compareEqualValues(left, right, equal)) {
                return makeExprError(ErrorCode::R002, opSpan,
                                     "类型不匹配：无法比较 " +
                                         actualTypeName(left) + " 与 " +
                                         actualTypeName(right));
            }
            if (op == BinaryOp::NotEqual) {
                equal = !equal;
            }
            return makeValue(Value::makeBool(equal));
        }
        case BinaryOp::Less:
        case BinaryOp::LessEqual:
        case BinaryOp::Greater:
        case BinaryOp::GreaterEqual: {
            int comparison = 0;
            if (!compareOrderValues(left, right, comparison)) {
                return makeExprError(ErrorCode::R002, opSpan,
                                     "类型不匹配：无法排序比较 " +
                                         actualTypeName(left) + " 与 " +
                                         actualTypeName(right));
            }
            bool result = false;
            switch (op) {
            case BinaryOp::Less: result = comparison < 0; break;
            case BinaryOp::LessEqual: result = comparison <= 0; break;
            case BinaryOp::Greater: result = comparison > 0; break;
            case BinaryOp::GreaterEqual: result = comparison >= 0; break;
            default: break;
            }
            return makeValue(Value::makeBool(result));
        }
        case BinaryOp::LogicalAnd:
        case BinaryOp::LogicalOr:
            break;
        }
        return makeExprError(ErrorCode::R002, opSpan, "未知二元运算");
    }

    EvalExprResult evalTernary(const Ternary& ternary) {
        EvalExprResult cond = evalExpr(*ternary.cond);
        if (!cond.ok()) {
            return cond;
        }
        if (!cond.value->isBool()) {
            return makeExprError(
                ErrorCode::R002, ternary.cond->span,
                "条件必须是 bool，实际为 " + actualTypeName(*cond.value));
        }
        if (cond.value->asBool()) {
            return evalExpr(*ternary.thenExpr);
        }
        return evalExpr(*ternary.elseExpr);
    }

    EvalExprResult evalCall(const Call& call) {
        EvalExprResult callee = evalExpr(*call.callee);
        if (!callee.ok()) {
            return callee;
        }
        if (!callee.value->isCallable()) {
            return makeExprError(
                ErrorCode::R012, call.callee->span,
                actualTypeName(*callee.value) + " 不可调用");
        }

        std::vector<Value> arguments;
        arguments.reserve(call.args.size());
        CallContext context;
        context.calleeSpan = call.callee->span;
        context.lparenSpan = call.lparenSpan;
        context.argSpans.reserve(call.args.size());
        context.source = context_.source;

        for (const ExprPtr& argument : call.args) {
            EvalExprResult value = evalExpr(*argument);
            if (!value.ok()) {
                return value;
            }
            arguments.push_back(std::move(*value.value));
            context.argSpans.push_back(argument->span);
        }

        CallResult result = callee.value->asCallable()->call(arguments, context);
        if (result.diagnostic) {
            EvalExprResult error;
            error.diagnostic = std::move(result.diagnostic);
            return error;
        }
        if (!result.value) {
            return makeExprError(ErrorCode::R002, call.callee->span,
                                 "callable 没有返回值");
        }
        return makeValue(std::move(*result.value));
    }

    EvalExprResult evalIndex(const Index& index) {
        EvalExprResult receiver = evalExpr(*index.receiver);
        if (!receiver.ok()) {
            return receiver;
        }
        EvalExprResult position = evalExpr(*index.index);
        if (!position.ok()) {
            return position;
        }
        if (!position.value->isInt()) {
            return makeExprError(
                ErrorCode::R002, index.index->span,
                "索引必须是 int，实际为 " +
                    actualTypeName(*position.value));
        }

        const std::int64_t rawIndex = position.value->asInt();
        if (rawIndex < 0) {
            return makeExprError(ErrorCode::R003, index.index->span,
                                 "索引越界");
        }
        const std::size_t positionValue =
            static_cast<std::size_t>(rawIndex);

        switch (receiver.value->type()) {
        case ValueType::List: {
            const auto& items = receiver.value->asList().items();
            if (positionValue >= items.size()) {
                return makeExprError(ErrorCode::R003, index.index->span,
                                     "索引越界");
            }
            return makeValue(items[positionValue]);
        }
        case ValueType::String: {
            const std::optional<std::string> codePoint =
                utf8At(receiver.value->asString(), positionValue);
            if (!codePoint) {
                return makeExprError(ErrorCode::R003, index.index->span,
                                     "索引越界");
            }
            return makeValue(Value::makeString(*codePoint));
        }
        case ValueType::Point: {
            const PointValue& point = receiver.value->asPoint();
            if (positionValue >= static_cast<std::size_t>(point.dim)) {
                return makeExprError(ErrorCode::R003, index.index->span,
                                     "索引越界");
            }
            return makeValue(Value::makeDouble(point[positionValue]));
        }
        case ValueType::Int:
        case ValueType::Double:
        case ValueType::Bool:
        case ValueType::Callable:
        case ValueType::TypeToken:
            return makeExprError(
                ErrorCode::R002, index.index->span,
                actualTypeName(*receiver.value) + " 不可索引");
        }
        return makeExprError(ErrorCode::R002, index.index->span,
                             "不可索引");
    }

    EvalExprResult evalMember(const Member& member) {
        EvalExprResult receiver = evalExpr(*member.receiver);
        if (!receiver.ok()) {
            return receiver;
        }

        if (receiver.value->isPoint()) {
            const PointValue& point = receiver.value->asPoint();
            if (member.member.text == "x") {
                return makeValue(Value::makeDouble(point[0]));
            }
            if (member.member.text == "y") {
                return makeValue(Value::makeDouble(point[1]));
            }
            if (member.member.text == "z") {
                if (point.dim < 3) {
                    return makeExprError(ErrorCode::R003, member.member.span,
                                         "2 维点没有 z");
                }
                return makeValue(Value::makeDouble(point[2]));
            }
            return makeExprError(ErrorCode::R002, member.member.span,
                                 "Point 没有成员 " + member.member.text);
        }

        if (receiver.value->isList() || receiver.value->isString()) {
            if (member.member.text == "length") {
                if (receiver.value->isList()) {
                    return makeValue(Value::makeInt(static_cast<std::int64_t>(
                        receiver.value->asList().size())));
                }
                return makeValue(Value::makeInt(static_cast<std::int64_t>(
                    utf8Length(receiver.value->asString()))));
            }
            return makeExprError(ErrorCode::R002, member.member.span,
                                 "值没有成员 " + member.member.text);
        }

        return makeExprError(
            ErrorCode::R002, member.member.span,
            actualTypeName(*receiver.value) + " 没有成员 " +
                member.member.text);
    }

    EvalExprResult evalIncDec(const IncDec& incDec) {
        TargetResult target = resolveTarget(*incDec.target, false);
        if (!target.ok()) {
            EvalExprResult error;
            error.diagnostic = std::move(target.diagnostic);
            return error;
        }

        EvalExprResult current = readLValue(*target.lvalue);
        if (!current.ok()) {
            return current;
        }
        if (!current.value->isInt()) {
            return makeExprError(
                ErrorCode::R002, incDec.opSpan,
                "++/-- 的目标必须是 int，实际为 " +
                    actualTypeName(*current.value));
        }

        const std::int64_t oldValue = current.value->asInt();
        const std::int64_t delta =
            incDec.op == IncDecOp::Increment ? 1 : -1;
        std::int64_t newValue = 0;
        if (!checkedAdd(oldValue, delta, newValue)) {
            return makeExprError(ErrorCode::R005, incDec.opSpan,
                                 "整数溢出");
        }

        if (incDec.prefix) {
            const std::optional<Diagnostic> writeError =
                writeLValueThrough(*target.lvalue,
                                   Value::makeInt(newValue), incDec.opSpan);
            if (writeError) {
                EvalExprResult error;
                error.diagnostic = writeError;
                return error;
            }
            return makeValue(Value::makeInt(newValue));
        }

        if (deferPostfix_) {
            pending_.push_back(PendingPostfix{std::move(*target.lvalue), delta,
                                              incDec.opSpan});
            return makeValue(Value::makeInt(oldValue));
        }

        const std::optional<Diagnostic> writeError =
            writeLValueThrough(*target.lvalue, Value::makeInt(newValue),
                               incDec.opSpan);
        if (writeError) {
            EvalExprResult error;
            error.diagnostic = writeError;
            return error;
        }
        return makeValue(Value::makeInt(oldValue));
    }

    EvalExprResult evalDeref(const Deref& deref) {
        auto cell = context_.env.find(deref.variable->name.text);
        if (!cell) {
            return makeExprError(ErrorCode::R001, deref.variable->name.span,
                                 "未定义变量 " +
                                     deref.variable->name.text);
        }
        if (!cell->isRef()) {
            return makeExprError(ErrorCode::R007, deref.starSpan,
                                 deref.variable->name.text + " 不是引用");
        }
        return makeValue(cell->refTarget()->readCopy());
    }

    EvalExprResult evalPoint(const Point& point) {
        if (point.coordinates.size() != 2 &&
            point.coordinates.size() != 3) {
            return makeExprError(ErrorCode::R006, point.span,
                                 "Point 只能有 2 或 3 个坐标");
        }

        PointValue result;
        result.dim = static_cast<int>(point.coordinates.size());
        for (std::size_t index = 0; index < point.coordinates.size();
             ++index) {
            EvalExprResult value = evalExpr(*point.coordinates[index]);
            if (!value.ok()) {
                return value;
            }
            double coordinate = 0.0;
            if (!asDouble(*value.value, coordinate)) {
                return makeExprError(
                    ErrorCode::R006, point.coordinates[index]->span,
                    "Point 坐标必须是数值，实际为 " +
                        actualTypeName(*value.value));
            }
            result.coordinates[index] = coordinate;
        }
        return makeValue(Value::makePoint(result));
    }

    EvalExprResult evalList(const List& list) {
        std::vector<Value> items;
        items.reserve(list.elements.size());
        for (const ExprPtr& element : list.elements) {
            EvalExprResult value = evalExpr(*element);
            if (!value.ok()) {
                return value;
            }
            items.push_back(std::move(*value.value));
        }
        return makeValue(Value::makeList(ListValue(std::move(items))));
    }

    EvalFlowResult evalAssign(const Assign& assign) {
        const bool previousDefer = deferPostfix_;
        deferPostfix_ = true;
        TargetResult target = resolveTarget(*assign.target, true);
        deferPostfix_ = previousDefer;

        if (!target.ok()) {
            std::optional<Diagnostic> diagnostic =
                commitPending(std::move(target.diagnostic));
            if (diagnostic) {
                return makeFlowError(std::move(*diagnostic));
            }
            return makeFlow();
        }

        EvalExprResult value = evalExpr(*assign.value);
        if (!value.ok()) {
            std::optional<Diagnostic> diagnostic =
                commitPending(std::move(value.diagnostic));
            if (diagnostic) {
                return makeFlowError(std::move(*diagnostic));
            }
            return makeFlow();
        }

        std::optional<Diagnostic> writeError =
            writeLValue(*target.lvalue, std::move(*value.value),
                        assign.value->span);
        std::optional<Diagnostic> diagnostic =
            commitPending(std::move(writeError));
        if (diagnostic) {
            return makeFlowError(std::move(*diagnostic));
        }
        return makeFlow();
    }

    EvalFlowResult evalRefAssign(const RefAssign& assign) {
        auto source = context_.env.find(assign.source.text);
        if (!source) {
            return makeFlowError(
                makeDiagnostic(ErrorCode::R001, assign.source.span,
                               "未定义变量 " + assign.source.text));
        }
        if (source->isRef()) {
            return makeFlowError(
                makeDiagnostic(ErrorCode::R008, assign.source.span,
                               "不能引用一个引用"));
        }

        auto target = context_.env.find(assign.target.text);
        if (target && target == source) {
            return makeFlowError(
                makeDiagnostic(ErrorCode::R008, assign.source.span,
                               "不能引用自身"));
        }
        if (target && target->hasInboundRef()) {
            return makeFlowError(
                makeDiagnostic(ErrorCode::R008, assign.target.span,
                               "被引用的变量不能再改绑为引用"));
        }

        if (!target) {
            context_.env.createCell(assign.target.text,
                                    Cell::makeRef(source));
        } else {
            target->bindRef(source);
        }
        source->markInboundRef();
        return makeFlow();
    }

    EvalFlowResult evalCommand(const Command& command) {
        std::vector<Value> arguments;
        arguments.reserve(command.args.size());
        CommandContext context;
        context.nameSpan = command.name.span;
        context.nameSource = context_.source;
        context.argSpans.reserve(command.args.size());
        context.argSources.reserve(command.args.size());

        for (const ExprPtr& argument : command.args) {
            EvalExprResult value = evalExpr(*argument);
            if (!value.ok()) {
                return makeFlowError(std::move(*value.diagnostic));
            }
            arguments.push_back(std::move(*value.value));
            context.argSpans.push_back(argument->span);
            context.argSources.push_back(context_.source);
        }

        std::optional<Diagnostic> diagnostic = context_.names.invokeCommand(
            command.name.text, arguments, context_.output, context);
        if (diagnostic) {
            return makeFlowError(std::move(*diagnostic));
        }
        return makeFlow();
    }

    TargetResult resolveTarget(const Expr& expression, bool allowCreate) {
        return resolveTargetExpr(expression, allowCreate);
    }

    TargetResult resolveTargetExpr(const Expr& expression, bool allowCreate) {
        switch (expression.kind()) {
        case NodeKind::Variable: {
            const auto& variable = static_cast<const Variable&>(expression);
            auto cell = context_.env.find(variable.name.text);
            if (cell) {
                LValue value;
                value.span = expression.span;
                value.simpleVariable = true;
                value.binding = cell;
                value.storage = cell->effective();
                return TargetResult{std::move(value), std::nullopt};
            }
            if (allowCreate) {
                LValue value;
                value.span = expression.span;
                value.simpleVariable = true;
                value.createOnWrite = true;
                value.bindingName = variable.name.text;
                return TargetResult{std::move(value), std::nullopt};
            }
            return TargetResult{
                std::nullopt,
                makeDiagnostic(ErrorCode::R001, variable.name.span,
                               "未定义变量 " + variable.name.text)};
        }
        case NodeKind::Deref: {
            const auto& deref = static_cast<const Deref&>(expression);
            auto cell = context_.env.find(deref.variable->name.text);
            if (!cell) {
                return TargetResult{
                    std::nullopt,
                    makeDiagnostic(ErrorCode::R001,
                                   deref.variable->name.span,
                                   "未定义变量 " +
                                       deref.variable->name.text)};
            }
            if (!cell->isRef()) {
                return TargetResult{
                    std::nullopt,
                    makeDiagnostic(ErrorCode::R007, deref.starSpan,
                                   deref.variable->name.text +
                                       " 不是引用")};
            }
            LValue value;
            value.span = expression.span;
            value.storage = cell->refTarget();
            return TargetResult{std::move(value), std::nullopt};
        }
        case NodeKind::Index:
            return resolveIndexTarget(static_cast<const Index&>(expression));
        case NodeKind::Member:
            return resolveMemberTarget(
                static_cast<const Member&>(expression));
        default:
            return TargetResult{
                std::nullopt,
                makeDiagnostic(ErrorCode::R002, expression.span,
                               "赋值目标不可写")};
        }
    }

    TargetResult resolveIndexTarget(const Index& index) {
        TargetResult receiver = resolveTargetExpr(*index.receiver, false);
        if (!receiver.ok()) {
            return receiver;
        }

        EvalExprResult current = readLValue(*receiver.lvalue);
        if (!current.ok()) {
            return TargetResult{std::nullopt,
                                std::move(current.diagnostic)};
        }

        EvalExprResult position = evalExpr(*index.index);
        if (!position.ok()) {
            return TargetResult{std::nullopt,
                                std::move(position.diagnostic)};
        }
        if (!position.value->isInt()) {
            return TargetResult{
                std::nullopt,
                makeDiagnostic(ErrorCode::R002, index.index->span,
                               "索引必须是 int，实际为 " +
                                   actualTypeName(*position.value))};
        }

        const std::int64_t rawIndex = position.value->asInt();
        if (rawIndex < 0) {
            return TargetResult{
                std::nullopt,
                makeDiagnostic(ErrorCode::R003, index.index->span,
                               "索引越界")};
        }
        const std::size_t positionValue =
            static_cast<std::size_t>(rawIndex);

        LValue value = std::move(*receiver.lvalue);
        value.span = index.span;
        value.simpleVariable = false;
        value.createOnWrite = false;

        if (current.value->isList()) {
            if (positionValue >= current.value->asList().size()) {
                return TargetResult{
                    std::nullopt,
                    makeDiagnostic(ErrorCode::R003, index.index->span,
                                   "索引越界")};
            }
            value.path.push_back(Step{StepKind::ListIndex, positionValue});
            return TargetResult{std::move(value), std::nullopt};
        }
        if (current.value->isString()) {
            return TargetResult{
                std::nullopt,
                makeDiagnostic(ErrorCode::R002, index.index->span,
                               "字符串元素只读")};
        }
        if (current.value->isPoint()) {
            const int dimension = current.value->asPoint().dim;
            if (rawIndex >= dimension) {
                return TargetResult{
                    std::nullopt,
                    makeDiagnostic(ErrorCode::R003, index.index->span,
                                   "索引越界")};
            }
            value.path.push_back(
                Step{StepKind::PointIndex, positionValue});
            return TargetResult{std::move(value), std::nullopt};
        }

        return TargetResult{
            std::nullopt,
            makeDiagnostic(ErrorCode::R002, index.index->span,
                           actualTypeName(*current.value) + " 不可索引")};
    }

    TargetResult resolveMemberTarget(const Member& member) {
        TargetResult receiver = resolveTargetExpr(*member.receiver, false);
        if (!receiver.ok()) {
            return receiver;
        }

        EvalExprResult current = readLValue(*receiver.lvalue);
        if (!current.ok()) {
            return TargetResult{std::nullopt,
                                std::move(current.diagnostic)};
        }

        LValue value = std::move(*receiver.lvalue);
        value.span = member.span;
        value.simpleVariable = false;
        value.createOnWrite = false;

        if (current.value->isPoint()) {
            const PointValue& point = current.value->asPoint();
            if (member.member.text == "x") {
                value.path.push_back(Step{StepKind::PointX, 0});
                return TargetResult{std::move(value), std::nullopt};
            }
            if (member.member.text == "y") {
                value.path.push_back(Step{StepKind::PointY, 1});
                return TargetResult{std::move(value), std::nullopt};
            }
            if (member.member.text == "z") {
                if (point.dim < 3) {
                    return TargetResult{
                        std::nullopt,
                        makeDiagnostic(ErrorCode::R003,
                                       member.member.span,
                                       "2 维点没有 z")};
                }
                value.path.push_back(Step{StepKind::PointZ, 2});
                return TargetResult{std::move(value), std::nullopt};
            }
            return TargetResult{
                std::nullopt,
                makeDiagnostic(ErrorCode::R002, member.member.span,
                               "Point 没有成员 " + member.member.text)};
        }

        if (current.value->isList() || current.value->isString()) {
            if (member.member.text == "length") {
                return TargetResult{
                    std::nullopt,
                    makeDiagnostic(ErrorCode::R002, member.member.span,
                                   ".length 是只读成员")};
            }
            return TargetResult{
                std::nullopt,
                makeDiagnostic(ErrorCode::R002, member.member.span,
                               "值没有成员 " + member.member.text)};
        }

        return TargetResult{
            std::nullopt,
            makeDiagnostic(ErrorCode::R002, member.member.span,
                           actualTypeName(*current.value) + " 没有成员 " +
                               member.member.text)};
    }

    EvalExprResult readLValue(const LValue& lvalue) {
        if (lvalue.simpleVariable) {
            if (lvalue.createOnWrite || !lvalue.storage) {
                return makeExprError(ErrorCode::R001, lvalue.span,
                                     "未定义变量");
            }
            return makeValue(lvalue.storage->readCopy());
        }

        if (!lvalue.storage) {
            return makeExprError(ErrorCode::R001, lvalue.span,
                                 "未定义变量");
        }

        Value current = lvalue.storage->readCopy();
        for (const Step& step : lvalue.path) {
            if (!applyReadStep(current, step)) {
                return makeExprError(ErrorCode::R003, lvalue.span,
                                     "索引越界");
            }
        }
        return makeValue(std::move(current));
    }

    bool applyReadStep(Value& current, const Step& step) {
        switch (step.kind) {
        case StepKind::ListIndex: {
            if (!current.isList()) {
                return false;
            }
            const auto& items = current.asList().items();
            if (step.index >= items.size()) {
                return false;
            }
            current = items[step.index];
            return true;
        }
        case StepKind::PointIndex: {
            if (!current.isPoint()) {
                return false;
            }
            const PointValue& point = current.asPoint();
            if (step.index >= static_cast<std::size_t>(point.dim)) {
                return false;
            }
            current = Value::makeDouble(point[step.index]);
            return true;
        }
        case StepKind::PointX:
        case StepKind::PointY:
        case StepKind::PointZ: {
            if (!current.isPoint()) {
                return false;
            }
            const PointValue& point = current.asPoint();
            if (step.kind == StepKind::PointZ && point.dim < 3) {
                return false;
            }
            current = Value::makeDouble(point[step.index]);
            return true;
        }
        }
        return false;
    }

    std::optional<Diagnostic> writeLValue(const LValue& lvalue, Value value,
                                          Span rhsSpan) {
        if (lvalue.simpleVariable) {
            if (lvalue.createOnWrite) {
                context_.env.create(lvalue.bindingName, std::move(value));
                return std::nullopt;
            }
            if (!lvalue.binding) {
                return makeDiagnostic(ErrorCode::R001, lvalue.span,
                                      "未定义变量");
            }
            lvalue.binding->rebind(std::move(value));
            return std::nullopt;
        }

        if (!lvalue.storage) {
            return makeDiagnostic(ErrorCode::R001, lvalue.span,
                                  "未定义变量");
        }
        return writePath(lvalue.storage, lvalue.path, std::move(value),
                         rhsSpan);
    }

    std::optional<Diagnostic> writeLValueThrough(const LValue& lvalue,
                                                 Value value, Span rhsSpan) {
        if (lvalue.simpleVariable) {
            if (!lvalue.storage) {
                return makeDiagnostic(ErrorCode::R001, lvalue.span,
                                      "未定义变量");
            }
            lvalue.storage->rebind(std::move(value));
            return std::nullopt;
        }

        if (!lvalue.storage) {
            return makeDiagnostic(ErrorCode::R001, lvalue.span,
                                  "未定义变量");
        }
        return writePath(lvalue.storage, lvalue.path, std::move(value),
                         rhsSpan);
    }

    std::optional<Diagnostic> writePath(std::shared_ptr<Cell> storage,
                                        const std::vector<Step>& path,
                                        Value value, Span rhsSpan) {
        if (path.empty()) {
            storage->rebind(std::move(value));
            return std::nullopt;
        }

        Value* current = &storage->value();
        for (std::size_t index = 0; index < path.size(); ++index) {
            const Step& step = path[index];
            const bool last = index + 1 == path.size();

            if (step.kind == StepKind::ListIndex) {
                if (!current->isList()) {
                    return makeDiagnostic(ErrorCode::R002, rhsSpan,
                                          "目标不是 list");
                }
                auto& items = current->asList().items();
                if (step.index >= items.size()) {
                    return makeDiagnostic(ErrorCode::R003, rhsSpan,
                                          "索引越界");
                }
                if (last) {
                    items[step.index] = std::move(value);
                    return std::nullopt;
                }
                current = &items[step.index];
                continue;
            }

            if (!current->isPoint()) {
                return makeDiagnostic(ErrorCode::R002, rhsSpan,
                                      "目标不是 point");
            }
            PointValue& point = current->asPoint();
            if (!last) {
                return makeDiagnostic(ErrorCode::R002, rhsSpan,
                                      "point 分量不能继续索引");
            }
            if (step.kind == StepKind::PointZ && point.dim < 3) {
                return makeDiagnostic(ErrorCode::R003, rhsSpan,
                                      "2 维点没有 z");
            }
            if (step.index >= static_cast<std::size_t>(point.dim)) {
                return makeDiagnostic(ErrorCode::R003, rhsSpan,
                                      "索引越界");
            }

            double coordinate = 0.0;
            if (!asDouble(value, coordinate)) {
                return makeDiagnostic(
                    ErrorCode::R002, rhsSpan,
                    "point 分量必须是数值，实际为 " +
                        actualTypeName(value));
            }
            point[step.index] = coordinate;
            return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<Diagnostic> commitPending(
        std::optional<Diagnostic> prior) {
        std::vector<PendingPostfix> pending = std::move(pending_);
        pending_.clear();

        for (PendingPostfix& update : pending) {
            EvalExprResult current = readLValue(update.target);
            if (!current.ok()) {
                if (!prior) {
                    prior = std::move(current.diagnostic);
                }
                continue;
            }
            if (!current.value->isInt()) {
                if (!prior) {
                    prior = makeDiagnostic(
                        ErrorCode::R002, update.opSpan,
                        "++/-- 的目标必须是 int");
                }
                continue;
            }

            std::int64_t newValue = 0;
            if (!checkedAdd(current.value->asInt(), update.delta,
                            newValue)) {
                if (!prior) {
                    prior = makeDiagnostic(ErrorCode::R005, update.opSpan,
                                           "整数溢出");
                }
                continue;
            }

            const std::optional<Diagnostic> writeError =
                writeLValueThrough(update.target,
                                   Value::makeInt(newValue), update.opSpan);
            if (writeError && !prior) {
                prior = writeError;
            }
        }

        return prior;
    }

    EvalContext& context_;
    bool deferPostfix_ = false;
    std::vector<PendingPostfix> pending_;
};

} // namespace

EvalResult evalProgram(const Program& program, EvalContext& context) {
    Evaluator evaluator(context);
    EvalFlowResult result = evaluator.evalNode(program);
    if (result.diagnostic) {
        return EvalResult{std::move(result.diagnostic)};
    }
    if (result.flow != Flow::Normal) {
        return EvalResult{makeDiagnostic(
            ErrorCode::S005, program.span,
            "break/continue 不在循环内")};
    }
    return EvalResult{};
}

EvalExpressionResult evalExpression(const Expr& expression,
                                    EvalContext& context) {
    Evaluator evaluator(context);
    EvalExprResult result = evaluator.evaluateExpression(expression);
    if (result.diagnostic) {
        return EvalExpressionResult{std::nullopt,
                                    std::move(result.diagnostic)};
    }
    if (!result.value) {
        return EvalExpressionResult{
            std::nullopt,
            makeDiagnostic(ErrorCode::R002, expression.span,
                           "表达式没有返回值")};
    }
    return EvalExpressionResult{std::move(result.value), std::nullopt};
}

} // namespace lush::core

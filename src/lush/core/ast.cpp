#include "lush/core/ast.h"

#include <charconv>
#include <sstream>
#include <string>
#include <system_error>

namespace lush::core {
namespace {

std::string escapeString(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    constexpr char kHex[] = "0123456789ABCDEF";

    for (unsigned char value : text) {
        switch (value) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\0':
            escaped += "\\0";
            break;
        default:
            if (value < 0x20U || value == 0x7FU) {
                escaped += "\\u00";
                escaped.push_back(kHex[(value >> 4) & 0x0FU]);
                escaped.push_back(kHex[value & 0x0FU]);
            } else {
                escaped.push_back(static_cast<char>(value));
            }
            break;
        }
    }

    return escaped;
}

std::string formatDouble(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::general);
    if (result.ec != std::errc{}) {
        return "0";
    }
    return std::string(buffer, result.ptr);
}

class DumpVisitor final : public AstVisitor {
public:
    DumpVisitor(const Source& source, std::ostringstream& out)
        : source_(source), out_(out) {}

    void visit(const Program& node) override {
        header("Program", node.span);
        children(node.statements);
    }

    void visit(const Block& node) override {
        header("Block", node.span);
        children(node.statements);
    }

    void visit(const If& node) override {
        header("If", node.span);
        child("cond", node.cond.get());
        child("then", node.thenBranch.get());
        if (node.elseBranch) {
            child("else", node.elseBranch.get());
        } else {
            none("else");
        }
    }

    void visit(const While& node) override {
        header("While", node.span);
        child("cond", node.cond.get());
        child("body", node.body.get());
    }

    void visit(const ForIn& node) override {
        header("ForIn var=\"" + escapeString(node.var.text) + "\"", node.span);
        child("iterable", node.iterable.get());
        child("body", node.body.get());
    }

    void visit(const For& node) override {
        header("For", node.span);
        if (node.init) {
            child("init", node.init.get());
        } else {
            none("init");
        }
        if (node.cond) {
            child("cond", node.cond.get());
        } else {
            none("cond");
        }
        if (node.step) {
            child("step", node.step.get());
        } else {
            none("step");
        }
        child("body", node.body.get());
    }

    void visit(const Break& node) override { header("Break", node.span); }
    void visit(const Continue& node) override { header("Continue", node.span); }

    void visit(const Assign& node) override {
        header("Assign", node.span);
        child("target", node.target.get());
        child("value", node.value.get());
    }

    void visit(const RefAssign& node) override {
        header("RefAssign target=\"" + escapeString(node.target.text) + "\" source=\"" +
                   escapeString(node.source.text) + "\"",
               node.span);
    }

    void visit(const Command& node) override {
        header("Command name=\"" + escapeString(node.name.text) + "\"", node.span);
        for (const ExprPtr& arg : node.args) {
            child(arg.get());
        }
    }

    void visit(const EmptyStmt& node) override { header("EmptyStmt", node.span); }

    void visit(const Int& node) override {
        header("Int " + std::to_string(node.value), node.span);
    }

    void visit(const BoundaryInt& node) override { header("BoundaryInt", node.span); }

    void visit(const Float& node) override {
        header("Float " + formatDouble(node.value), node.span);
    }

    void visit(const String& node) override {
        header("String \"" + escapeString(node.value) + "\"", node.span);
    }

    void visit(const Bool& node) override {
        header(std::string("Bool ") + (node.value ? "true" : "false"), node.span);
    }

    void visit(const Variable& node) override {
        header("Variable name=\"" + escapeString(node.name.text) + "\"", node.span);
    }

    void visit(const Unary& node) override {
        header("Unary op=\"" + std::string(unaryOpName(node.op)) + "\"", node.span);
        child("operand", node.operand.get());
    }

    void visit(const Binary& node) override {
        header("Binary op=\"" + std::string(binaryOpName(node.op)) + "\"", node.span);
        child("left", node.left.get());
        child("right", node.right.get());
    }

    void visit(const Ternary& node) override {
        header("Ternary", node.span);
        child("cond", node.cond.get());
        child("then", node.thenExpr.get());
        child("else", node.elseExpr.get());
    }

    void visit(const Call& node) override {
        header("Call", node.span);
        child("callee", node.callee.get());
        if (node.args.empty()) {
            none("args");
        } else {
            ++depth_;
            indent();
            out_ << "args:\n";
            ++depth_;
            for (const ExprPtr& arg : node.args) {
                arg->accept(*this);
            }
            depth_ -= 2;
        }
    }

    void visit(const Index& node) override {
        header("Index", node.span);
        child("receiver", node.receiver.get());
        child("index", node.index.get());
    }

    void visit(const Member& node) override {
        header("Member name=\"" + escapeString(node.member.text) + "\"", node.span);
        child("receiver", node.receiver.get());
    }

    void visit(const IncDec& node) override {
        header("IncDec op=\"" + std::string(incDecOpName(node.op)) + "\" prefix=" +
                   (node.prefix ? "true" : "false"),
               node.span);
        child("target", node.target.get());
    }

    void visit(const Deref& node) override {
        header("Deref", node.span);
        child("target", node.variable.get());
    }

    void visit(const Point& node) override {
        header(std::string("Point bare=") + (node.bare ? "true" : "false"), node.span);
        for (const ExprPtr& coordinate : node.coordinates) {
            child(coordinate.get());
        }
    }

    void visit(const List& node) override {
        header("List", node.span);
        for (const ExprPtr& element : node.elements) {
            child(element.get());
        }
    }

private:
    void indent() { out_ << std::string(depth_ * 2, ' '); }

    void header(const std::string& text, Span span) {
        indent();
        out_ << text << ' ';
        writeSpan(span);
        out_ << '\n';
    }

    void writeSpan(Span span) {
        const LineCol start = source_.lineCol(span.start);
        const LineCol end = source_.lineCol(span.end);
        out_ << '@' << start.line << ':' << start.column << ".." << end.line << ':'
             << end.column;
    }

    void child(const char* label, const Node* node) {
        ++depth_;
        indent();
        out_ << label << ":\n";
        ++depth_;
        node->accept(*this);
        depth_ -= 2;
    }

    void child(const Node* node) {
        ++depth_;
        node->accept(*this);
        --depth_;
    }

    void children(const std::vector<StmtPtr>& statements) {
        for (const StmtPtr& statement : statements) {
            child(statement.get());
        }
    }

    void none(const char* label) {
        ++depth_;
        indent();
        out_ << label << ": <none>\n";
        --depth_;
    }

    const Source& source_;
    std::ostringstream& out_;
    std::size_t depth_ = 0;
};

} // namespace

std::string_view unaryOpName(UnaryOp op) noexcept {
    switch (op) {
    case UnaryOp::Negate: return "-";
    case UnaryOp::Not: return "!";
    }
    return "?";
}

std::string_view binaryOpName(BinaryOp op) noexcept {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Subtract: return "-";
    case BinaryOp::Multiply: return "*";
    case BinaryOp::Divide: return "/";
    case BinaryOp::Modulo: return "%";
    case BinaryOp::Equal: return "==";
    case BinaryOp::NotEqual: return "!=";
    case BinaryOp::Less: return "<";
    case BinaryOp::LessEqual: return "<=";
    case BinaryOp::Greater: return ">";
    case BinaryOp::GreaterEqual: return ">=";
    case BinaryOp::LogicalAnd: return "&&";
    case BinaryOp::LogicalOr: return "||";
    }
    return "?";
}

std::string_view incDecOpName(IncDecOp op) noexcept {
    switch (op) {
    case IncDecOp::Increment: return "++";
    case IncDecOp::Decrement: return "--";
    }
    return "?";
}

std::string dumpAst(const Program& program, const Source& source) {
    std::ostringstream out;
    DumpVisitor visitor(source, out);
    program.accept(visitor);
    return out.str();
}

std::string dumpExpr(const Expr& expression, const Source& source) {
    std::ostringstream out;
    DumpVisitor visitor(source, out);
    expression.accept(visitor);
    return out.str();
}

} // namespace lush::core

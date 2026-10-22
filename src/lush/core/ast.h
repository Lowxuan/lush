#pragma once

#include "lush/core/source.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lush::core {

struct Name {
    std::string text;
    Span span{};
};

enum class UnaryOp {
    Negate,
    Not,
};

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LogicalAnd,
    LogicalOr,
};

enum class IncDecOp {
    Increment,
    Decrement,
};

std::string_view unaryOpName(UnaryOp op) noexcept;
std::string_view binaryOpName(BinaryOp op) noexcept;
std::string_view incDecOpName(IncDecOp op) noexcept;

struct Program;
struct Block;
struct If;
struct While;
struct ForIn;
struct For;
struct Break;
struct Continue;
struct Assign;
struct RefAssign;
struct Command;
struct EmptyStmt;
struct Int;
struct BoundaryInt;
struct Float;
struct String;
struct Bool;
struct Variable;
struct Unary;
struct Binary;
struct Ternary;
struct Call;
struct Index;
struct Member;
struct IncDec;
struct Deref;
struct Point;
struct List;

class AstVisitor {
public:
    virtual ~AstVisitor() = default;

    virtual void visit(const Program&) {}
    virtual void visit(const Block&) {}
    virtual void visit(const If&) {}
    virtual void visit(const While&) {}
    virtual void visit(const ForIn&) {}
    virtual void visit(const For&) {}
    virtual void visit(const Break&) {}
    virtual void visit(const Continue&) {}
    virtual void visit(const Assign&) {}
    virtual void visit(const RefAssign&) {}
    virtual void visit(const Command&) {}
    virtual void visit(const EmptyStmt&) {}
    virtual void visit(const Int&) {}
    virtual void visit(const BoundaryInt&) {}
    virtual void visit(const Float&) {}
    virtual void visit(const String&) {}
    virtual void visit(const Bool&) {}
    virtual void visit(const Variable&) {}
    virtual void visit(const Unary&) {}
    virtual void visit(const Binary&) {}
    virtual void visit(const Ternary&) {}
    virtual void visit(const Call&) {}
    virtual void visit(const Index&) {}
    virtual void visit(const Member&) {}
    virtual void visit(const IncDec&) {}
    virtual void visit(const Deref&) {}
    virtual void visit(const Point&) {}
    virtual void visit(const List&) {}
};

struct Node {
    Span span{};
    virtual ~Node() = default;
    virtual void accept(AstVisitor& visitor) const = 0;
};

struct Stmt : Node {};
struct Expr : Node {};

using StmtPtr = std::unique_ptr<Stmt>;
using ExprPtr = std::unique_ptr<Expr>;

struct Int : Expr {
    std::int64_t value = 0;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct BoundaryInt : Expr {
    Span literalSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Float : Expr {
    double value = 0.0;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct String : Expr {
    std::string value;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Bool : Expr {
    bool value = false;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Variable : Expr {
    Name name;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Unary : Expr {
    UnaryOp op = UnaryOp::Negate;
    Span opSpan{};
    ExprPtr operand;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Binary : Expr {
    BinaryOp op = BinaryOp::Add;
    Span opSpan{};
    ExprPtr left;
    ExprPtr right;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Ternary : Expr {
    ExprPtr cond;
    ExprPtr thenExpr;
    ExprPtr elseExpr;
    Span questionSpan{};
    Span colonSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Call : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
    Span lparenSpan{};
    Span rparenSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Index : Expr {
    ExprPtr receiver;
    ExprPtr index;
    Span openSpan{};
    Span closeSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Member : Expr {
    ExprPtr receiver;
    Name member;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct IncDec : Expr {
    IncDecOp op = IncDecOp::Increment;
    bool prefix = false;
    Span opSpan{};
    ExprPtr target;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Deref : Expr {
    Span starSpan{};
    std::unique_ptr<Variable> variable;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Point : Expr {
    std::vector<ExprPtr> coordinates;
    bool bare = false;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct List : Expr {
    std::vector<ExprPtr> elements;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Block : Stmt {
    std::vector<StmtPtr> statements;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct If : Stmt {
    ExprPtr cond;
    StmtPtr thenBranch;
    StmtPtr elseBranch;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct While : Stmt {
    ExprPtr cond;
    StmtPtr body;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Assign : Stmt {
    ExprPtr target;
    ExprPtr value;
    Span assignSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct RefAssign : Stmt {
    Name target;
    Name source;
    Span refSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Command : Stmt {
    Name name;
    std::vector<ExprPtr> args;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct EmptyStmt : Stmt {
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Break : Stmt {
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Continue : Stmt {
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct ForIn : Stmt {
    Name var;
    ExprPtr iterable;
    StmtPtr body;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct For : Stmt {
    std::unique_ptr<Assign> init;
    ExprPtr cond;
    ExprPtr step;
    StmtPtr body;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

struct Program : Node {
    std::vector<StmtPtr> statements;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
};

std::string dumpAst(const Program& program, const Source& source);
std::string dumpExpr(const Expr& expression, const Source& source);

} // namespace lush::core

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

enum class NodeKind {
    Program,
    Block,
    If,
    While,
    ForIn,
    For,
    Break,
    Continue,
    Assign,
    RefAssign,
    Command,
    EmptyStmt,
    Int,
    BoundaryInt,
    Float,
    String,
    Bool,
    Variable,
    Unary,
    Binary,
    Ternary,
    Call,
    Index,
    Member,
    IncDec,
    Deref,
    Point,
    List,
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
    virtual NodeKind kind() const noexcept = 0;
};

struct Stmt : Node {};
struct Expr : Node {};

using StmtPtr = std::unique_ptr<Stmt>;
using ExprPtr = std::unique_ptr<Expr>;

struct Int : Expr {
    std::int64_t value = 0;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Int; }
};

struct BoundaryInt : Expr {
    Span literalSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::BoundaryInt; }
};

struct Float : Expr {
    double value = 0.0;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Float; }
};

struct String : Expr {
    std::string value;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::String; }
};

struct Bool : Expr {
    bool value = false;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Bool; }
};

struct Variable : Expr {
    Name name;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Variable; }
};

struct Unary : Expr {
    UnaryOp op = UnaryOp::Negate;
    Span opSpan{};
    ExprPtr operand;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Unary; }
};

struct Binary : Expr {
    BinaryOp op = BinaryOp::Add;
    Span opSpan{};
    ExprPtr left;
    ExprPtr right;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Binary; }
};

struct Ternary : Expr {
    ExprPtr cond;
    ExprPtr thenExpr;
    ExprPtr elseExpr;
    Span questionSpan{};
    Span colonSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Ternary; }
};

struct Call : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
    Span lparenSpan{};
    Span rparenSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Call; }
};

struct Index : Expr {
    ExprPtr receiver;
    ExprPtr index;
    Span openSpan{};
    Span closeSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Index; }
};

struct Member : Expr {
    ExprPtr receiver;
    Name member;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Member; }
};

struct IncDec : Expr {
    IncDecOp op = IncDecOp::Increment;
    bool prefix = false;
    Span opSpan{};
    ExprPtr target;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::IncDec; }
};

struct Deref : Expr {
    Span starSpan{};
    std::unique_ptr<Variable> variable;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Deref; }
};

struct Point : Expr {
    std::vector<ExprPtr> coordinates;
    bool bare = false;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Point; }
};

struct List : Expr {
    std::vector<ExprPtr> elements;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::List; }
};

struct Block : Stmt {
    std::vector<StmtPtr> statements;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Block; }
};

struct If : Stmt {
    ExprPtr cond;
    StmtPtr thenBranch;
    StmtPtr elseBranch;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::If; }
};

struct While : Stmt {
    ExprPtr cond;
    StmtPtr body;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::While; }
};

struct Assign : Stmt {
    ExprPtr target;
    ExprPtr value;
    Span assignSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Assign; }
};

struct RefAssign : Stmt {
    Name target;
    Name source;
    Span refSpan{};
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::RefAssign; }
};

struct Command : Stmt {
    Name name;
    std::vector<ExprPtr> args;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Command; }
};

struct EmptyStmt : Stmt {
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::EmptyStmt; }
};

struct Break : Stmt {
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Break; }
};

struct Continue : Stmt {
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Continue; }
};

struct ForIn : Stmt {
    Name var;
    ExprPtr iterable;
    StmtPtr body;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::ForIn; }
};

struct For : Stmt {
    std::unique_ptr<Assign> init;
    ExprPtr cond;
    ExprPtr step;
    StmtPtr body;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::For; }
};

struct Program : Node {
    std::vector<StmtPtr> statements;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Program; }
};

std::string dumpAst(const Program& program, const Source& source);
std::string dumpExpr(const Expr& expression, const Source& source);

} // namespace lush::core

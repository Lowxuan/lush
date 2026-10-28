#pragma once

#include "lush/core/source.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// AST 与函数签名模型。
//
// 解析器只负责建立这里的结构，不做名称/类型校验；校验和求值分别由
// validate.cpp / eval.cpp 消费。Span 始终指向原始物理源文本，而不是
// 去掉续行后的逻辑文本。
namespace lush::core {

// 标识符及其原文位置。变量名大小写敏感；函数名由 FunctionTable 单独
// 规范化为大小写不敏感。
struct Name {
    std::string text;
    Span span{};
};

// 注解中的静态类型。Unknown 表示解析时看到了一个未知类型名，具体
// S011 诊断在 validate 阶段产生；省略注解则由上下文决定 any/void。
enum class TypeKind {
    Int,
    Double,
    Bool,
    String,
    List,
    Point,
    Point2,
    Point3,
    Any,
    Callable,
    Void,
    Unknown,
};

// 源码中的一个类型注解，text 保留用户原始拼写，kind 是规范化结果。
struct TypeAnnotation {
    TypeKind kind = TypeKind::Unknown;
    std::string text;
    Span span{};
};

// 用户函数的单个参数；type 为空表示省略注解，语义为 any。
struct FunctionParam {
    Name name;
    std::optional<TypeAnnotation> type;
    Span span{};
};

// 用户函数声明头。
//
// returnType 为空表示源码省略了返回注解，按 void 处理；显式返回 void
// 时也保留 TypeAnnotation{kind=Void}，因此 returnsValue() 对两种情况
// 都返回 false。函数体不放在签名里，签名表只需要声明模型。
struct FunctionSignature {
    Name name;
    std::vector<FunctionParam> params;
    std::optional<TypeAnnotation> returnType;
    Span span{};

    bool returnsValue() const noexcept {
        return returnType.has_value() && returnType->kind != TypeKind::Void;
    }
};

std::optional<TypeKind> typeKindFromName(std::string_view name) noexcept;
std::string_view typeKindName(TypeKind kind) noexcept;
bool isReservedTypeName(std::string_view name) noexcept;

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

// 稳定节点标识，供不依赖 dynamic_cast 的通用遍历和测试使用。
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
    Def,
    Return,
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
struct Def;
struct Return;
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

// AST 默认访问者。
//
// 所有 visit 都是空实现，子类只覆盖关心的节点；目前用于 AST dump 和
// BoundaryInt 的位置校验/规范化。访问者不拥有节点。
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
    virtual void visit(const Def&) {}
    virtual void visit(const Return&) {}
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

// 所有 AST 节点的基类：带源码范围，支持访问者和运行时 kind 查询。
struct Node {
    Span span{};
    virtual ~Node() = default;
    virtual void accept(AstVisitor& visitor) const = 0;
    virtual NodeKind kind() const noexcept = 0;
};

// 语句与表达式只用于类型区分，不增加状态。
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

// 顶层用户函数定义。M8a 只保存/校验；M8b 起 body 会被函数调用执行。
struct Def : Stmt {
    FunctionSignature signature;
    StmtPtr body;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Def; }
};

// 函数返回语句；value 为空表示 bare return。
struct Return : Stmt {
    ExprPtr value;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Return; }
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

// 一次解析/校验/执行的顶层语句序列；函数定义也作为普通 Stmt 保存。
struct Program : Node {
    std::vector<StmtPtr> statements;
    void accept(AstVisitor& visitor) const override { visitor.visit(*this); }
    NodeKind kind() const noexcept override { return NodeKind::Program; }
};

std::string dumpAst(const Program& program, const Source& source);
std::string dumpExpr(const Expr& expression, const Source& source);

} // namespace lush::core

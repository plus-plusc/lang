#pragma once

#include "lex.hxx"

#ifdef LLVM_AVAILABLE
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/IRBuilder.h>
#endif

namespace ast {

// Forward declarations for AST nodes
struct Expr;
struct Stmt;
struct Decl;

// Expression types
struct Expr {
    enum class Kind {
        IntegerLiteral,
        Identifier,
        BinaryOp,
        UnaryOp,
        Call,
        Cast,
        Match,
        Default
    };

    Kind kind;

    explicit Expr(Kind k) : kind(k) {}
    virtual ~Expr() = default;
};

struct IntegerLiteral : Expr {
    long long value;
    IntegerLiteral(long long v) : Expr{Kind::IntegerLiteral}, value(v) {}
};

struct Identifier : Expr {
    std::string name;
    Identifier(const std::string& n) : Expr{Kind::Identifier}, name(n) {}
};

struct BinaryOp : Expr {
    enum class Op { Add, Sub, Mul, Div, And, Or, Xor, Eq, Ne, Lt, Gt, Le, Ge };
    Op op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    BinaryOp(Op o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : Expr{Kind::BinaryOp}, op(o), left(std::move(l)), right(std::move(r)) {}
};

struct UnaryOp : Expr {
    enum class Op { Not, Neg };
    Op op;
    std::unique_ptr<Expr> operand;
    UnaryOp(Op o, std::unique_ptr<Expr> e) : Expr{Kind::UnaryOp}, op(o), operand(std::move(e)) {}
};

struct Call : Expr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    Call(const std::string& c, std::vector<std::unique_ptr<Expr>> a)
        : Expr{Kind::Call}, callee(c), args(std::move(a)) {}
};

// Statement types
struct Stmt {
    enum class Kind {
        Copy,
        Move,
        Return,
        Loop,
        Match,
        Goto,
        Label,
        Block,
        ExprStmt,
        DeclStmt
    };

    Kind kind;

    explicit Stmt(Kind k) : kind(k) {}
    virtual ~Stmt() = default;
};

struct ExprStmt : Stmt {
    std::unique_ptr<Expr> expr;
    ExprStmt(std::unique_ptr<Expr> e) : Stmt{Kind::ExprStmt}, expr(std::move(e)) {}
};

// Declaration
struct Decl {
    enum class Kind { VarDecl, FuncDecl };
    Kind kind;

    explicit Decl(Kind k) : kind(k) {}
    virtual ~Decl() = default;
};

struct DeclStmt : Stmt {
    std::unique_ptr<Decl> decl;
    DeclStmt(std::unique_ptr<Decl> d) : Stmt{Kind::DeclStmt}, decl(std::move(d)) {}
};

struct Copy : Stmt {
    std::string target;
    std::unique_ptr<Expr> value;
    Copy(const std::string& t, std::unique_ptr<Expr> v)
        : Stmt{Kind::Copy}, target(t), value(std::move(v)) {}
};

struct Move : Stmt {
    std::string target;
    std::unique_ptr<Expr> value;
    Move(const std::string& t, std::unique_ptr<Expr> v)
        : Stmt{Kind::Move}, target(t), value(std::move(v)) {}
};

struct Return : Stmt {
    std::unique_ptr<Expr> value;
    Return(std::unique_ptr<Expr> v) : Stmt{Kind::Return}, value(std::move(v)) {}
};

struct Loop : Stmt {
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Stmt> condition; // optional loop condition
    Loop(std::unique_ptr<Stmt> b, std::unique_ptr<Stmt> c = nullptr)
        : Stmt{Kind::Loop}, body(std::move(b)), condition(std::move(c)) {}
};

struct Goto : Stmt {
    std::string label;
    Goto(const std::string& l) : Stmt{Kind::Goto}, label(l) {}
};

struct Label : Stmt {
    std::string name;
    Label(const std::string& n) : Stmt{Kind::Label}, name(n) {}
};

struct Block : Stmt {
    std::vector<std::unique_ptr<Stmt>> statements;
    Block(std::vector<std::unique_ptr<Stmt>> s) : Stmt{Kind::Block}, statements(std::move(s)) {}
};

struct VarDecl : Decl {
    std::string name;
    std::string type;
    std::unique_ptr<Expr> initializer;
    VarDecl(const std::string& n, const std::string& t, std::unique_ptr<Expr> init)
        : Decl{Kind::VarDecl}, name(n), type(t), initializer(std::move(init)) {}
};

struct FuncDecl : Decl {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params; // name, type
    std::unique_ptr<Block> body;
    FuncDecl(const std::string& n, const std::string& rt,
             std::vector<std::pair<std::string, std::string>> p,
             std::unique_ptr<Block> b)
        : Decl{Kind::FuncDecl}, name(n), returnType(rt), params(std::move(p)), body(std::move(b)) {}
};

} // namespace ast

// Parser interface
std::unique_ptr<ast::Block> parse(std::vector<lex::Token> tokens);

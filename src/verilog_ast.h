// Tiny AST and interpreter for the combinational always block of an FSM.
//
// We only need to *evaluate* the next-state/output logic for a given present
// state and input vector, so the AST covers assignments, if/else and
// case/casex/casez with simple expressions -- the subset used by the contest
// benchmarks.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lexer.h"

namespace sva {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    enum class Kind { Literal, Identifier, BitSelect, Unary, Binary, Ternary };

    Kind kind = Kind::Literal;
    Value literal;              // Literal
    std::string name;           // Identifier, BitSelect
    std::string op;             // Unary, Binary
    std::vector<ExprPtr> args;  // operands; BitSelect uses args[0] as the index
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct CaseItem {
    std::vector<ExprPtr> labels;  // empty for the default item
    StmtPtr body;
};

struct Stmt {
    enum class Kind { Block, Assign, If, Case };

    Kind kind = Kind::Block;
    std::vector<StmtPtr> body;  // Block

    std::string target;  // Assign: target or target[target_bit]
    ExprPtr target_bit;
    ExprPtr value;

    ExprPtr condition;  // If
    StmtPtr then_branch;
    StmtPtr else_branch;

    std::string case_keyword;  // Case: "case", "casex" or "casez"
    ExprPtr selector;
    std::vector<CaseItem> items;
};

// Variable name -> current value (parameters, ports and regs).
using Env = std::unordered_map<std::string, Value>;

Value evaluate(const Expr& expr, const Env& env);
void execute(const Stmt& stmt, Env& env);

// casex/casez treat x/z/? label bits as wildcards; a plain case never matches
// a label containing x. casez is handled like casex.
bool case_matches(const std::string& keyword, const Value& selector, const Value& label);

}  // namespace sva

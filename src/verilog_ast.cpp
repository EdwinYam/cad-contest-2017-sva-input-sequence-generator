#include "verilog_ast.h"

#include <algorithm>

namespace sva {
namespace {

const Value& lookup(const Env& env, const std::string& name) {
    auto it = env.find(name);
    if (it == env.end()) throw ParseError("unknown identifier '" + name + "'");
    return it->second;
}

Value apply_binary(const std::string& op, const Value& a, const Value& b) {
    const int w = std::max(a.width, b.width);
    if (op == "==" || op == "===") return Value::known(a.bits == b.bits, 1);
    if (op == "!=" || op == "!==") return Value::known(a.bits != b.bits, 1);
    if (op == "&&") return Value::known(a.bits != 0 && b.bits != 0, 1);
    if (op == "||") return Value::known(a.bits != 0 || b.bits != 0, 1);
    if (op == "<") return Value::known(a.bits < b.bits, 1);
    if (op == "<=") return Value::known(a.bits <= b.bits, 1);
    if (op == ">") return Value::known(a.bits > b.bits, 1);
    if (op == ">=") return Value::known(a.bits >= b.bits, 1);
    if (op == "&") return Value::known(a.bits & b.bits, w);
    if (op == "|") return Value::known(a.bits | b.bits, w);
    if (op == "^") return Value::known(a.bits ^ b.bits, w);
    if (op == "+") return Value::known(a.bits + b.bits, w);
    if (op == "-") return Value::known(a.bits - b.bits, w);
    if (op == "*") return Value::known(a.bits * b.bits, w);
    if (op == "<<") return Value::known(a.bits << b.bits, a.width);
    if (op == ">>") return Value::known(a.bits >> b.bits, a.width);
    throw ParseError("unsupported operator '" + op + "'");
}

}  // namespace

Value evaluate(const Expr& expr, const Env& env) {
    switch (expr.kind) {
        case Expr::Kind::Literal:
            return expr.literal;
        case Expr::Kind::Identifier:
            return lookup(env, expr.name);
        case Expr::Kind::BitSelect: {
            const Value& v = lookup(env, expr.name);
            const uint64_t idx = evaluate(*expr.args[0], env).bits;
            if (idx >= static_cast<uint64_t>(v.width)) {
                throw ParseError("bit select out of range on '" + expr.name + "'");
            }
            return Value::known((v.bits >> idx) & 1U, 1);
        }
        case Expr::Kind::Unary: {
            const Value v = evaluate(*expr.args[0], env);
            if (expr.op == "!") return Value::known(v.bits == 0, 1);
            if (expr.op == "~") return Value::known(~v.bits, v.width);
            return Value::known(0 - v.bits, v.width);  // unary minus
        }
        case Expr::Kind::Binary:
            return apply_binary(expr.op, evaluate(*expr.args[0], env), evaluate(*expr.args[1], env));
        case Expr::Kind::Ternary:
            return evaluate(*expr.args[0], env).bits != 0 ? evaluate(*expr.args[1], env)
                                                           : evaluate(*expr.args[2], env);
    }
    return {};
}

bool case_matches(const std::string& keyword, const Value& selector, const Value& label) {
    const uint64_t m = width_mask(std::max(selector.width, label.width));
    // Bits above a value's own width are known zeros.
    const uint64_t label_care = (label.care | ~width_mask(label.width)) & m;
    uint64_t care = m;
    if (keyword == "case") {
        if (label_care != m) return false;
    } else {
        care = label_care;
    }
    return ((selector.bits ^ label.bits) & care) == 0;
}

void execute(const Stmt& stmt, Env& env) {
    switch (stmt.kind) {
        case Stmt::Kind::Block:
            for (const auto& s : stmt.body) execute(*s, env);
            return;
        case Stmt::Kind::Assign: {
            auto it = env.find(stmt.target);
            if (it == env.end()) throw ParseError("assignment to undeclared '" + stmt.target + "'");
            const Value rhs = evaluate(*stmt.value, env);
            Value& target = it->second;
            if (stmt.target_bit) {
                const uint64_t idx = evaluate(*stmt.target_bit, env).bits;
                if (idx >= static_cast<uint64_t>(target.width)) {
                    throw ParseError("bit select out of range on '" + stmt.target + "'");
                }
                target.bits = (target.bits & ~(1ULL << idx)) | ((rhs.bits & 1U) << idx);
            } else {
                target.bits = rhs.bits & width_mask(target.width);
            }
            return;
        }
        case Stmt::Kind::If:
            if (evaluate(*stmt.condition, env).bits != 0) {
                execute(*stmt.then_branch, env);
            } else if (stmt.else_branch) {
                execute(*stmt.else_branch, env);
            }
            return;
        case Stmt::Kind::Case: {
            const Value sel = evaluate(*stmt.selector, env);
            const Stmt* fallback = nullptr;
            for (const CaseItem& item : stmt.items) {
                if (item.labels.empty()) {
                    fallback = item.body.get();
                    continue;
                }
                for (const auto& label : item.labels) {
                    if (case_matches(stmt.case_keyword, sel, evaluate(*label, env))) {
                        execute(*item.body, env);
                        return;
                    }
                }
            }
            if (fallback) execute(*fallback, env);
            return;
        }
    }
}

}  // namespace sva

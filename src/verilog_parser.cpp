#include "verilog_parser.h"

#include <algorithm>

namespace sva {
namespace {

enum class Direction { Input, Output, Internal };

int binary_precedence(const std::string& op) {
    static const std::map<std::string, int> table = {
        {"||", 1}, {"&&", 2}, {"|", 3},  {"^", 4},  {"&", 5},  {"==", 6}, {"!=", 6},
        {"===", 6}, {"!==", 6}, {"<", 7}, {"<=", 7}, {">", 7}, {">=", 7}, {"<<", 8},
        {">>", 8}, {"+", 9},  {"-", 9},  {"*", 10}};
    auto it = table.find(op);
    return it == table.end() ? -1 : it->second;
}

// Strips begin/end wrappers that hold a single statement.
const Stmt& unwrap(const Stmt& s) {
    if (s.kind == Stmt::Kind::Block && s.body.size() == 1) return unwrap(*s.body[0]);
    return s;
}

class FsmParser {
public:
    explicit FsmParser(const std::string& source) : ts_(tokenize(source)) {}

    FsmModule parse() {
        ts_.expect("module");
        m_.name = ts_.expect_identifier();
        parse_header();
        while (!ts_.accept("endmodule")) {
            if (ts_.at_end()) ts_.fail("missing endmodule");
            parse_item();
        }
        finish();
        return std::move(m_);
    }

private:
    void parse_header() {
        ts_.expect("(");
        if (ts_.peek().text == "input" || ts_.peek().text == "output") {
            // ANSI style: module fsm(input clk, input [1:0] in, output reg out);
            while (!ts_.accept(")")) {
                const Direction dir = ts_.next().text == "input" ? Direction::Input : Direction::Output;
                parse_declarators(dir, /*ansi=*/true);
            }
        } else {
            // Plain style: the port names are declared again in the body.
            while (!ts_.accept(")")) ts_.next();
        }
        ts_.expect(";");
    }

    void parse_item() {
        const std::string& word = ts_.peek().text;
        if (word == "input" || word == "output") {
            const Direction dir = ts_.next().text == "input" ? Direction::Input : Direction::Output;
            parse_declarators(dir, /*ansi=*/false);
        } else if (word == "reg" || word == "wire" || word == "logic") {
            parse_declarators(Direction::Internal, /*ansi=*/false);
        } else if (word == "parameter" || word == "localparam") {
            parse_parameters();
        } else if (word == "always" || word == "always_comb" || word == "always_ff") {
            parse_always();
        } else if (word == "assert" || word == "property" ||
                   (ts_.peek().kind == TokenKind::Identifier && ts_.peek(1).text == ":")) {
            skip_assertion();
        } else {
            ts_.fail("unsupported module item '" + word + "'");
        }
    }

    // Reads "[reg] [range] a, b, c" after input/output/reg. In ANSI headers a
    // comma may be followed by a new direction keyword, which ends this group.
    void parse_declarators(Direction dir, bool ansi) {
        bool is_reg = dir == Direction::Internal;
        if (ts_.accept("reg") || ts_.accept("logic")) is_reg = true;
        ts_.accept("wire");
        const int width = parse_optional_range();
        for (;;) {
            declare(dir, ts_.expect_identifier(), width, is_reg);
            if (!ts_.accept(",")) break;
            if (ansi && (ts_.peek().text == "input" || ts_.peek().text == "output")) return;
        }
        if (!ansi) ts_.expect(";");
    }

    void declare(Direction dir, const std::string& name, int width, bool is_reg) {
        const Port port{name, width};
        if (dir == Direction::Input) m_.inputs.push_back(port);
        if (dir == Direction::Output) m_.outputs.push_back(port);
        if (is_reg || dir == Direction::Output) m_.regs[name] = port;
    }

    // Returns the width of an optional [msb:0] range (1 when absent).
    int parse_optional_range() {
        if (!ts_.accept("[")) return 1;
        const int msb = constant(*parse_expr());
        ts_.expect(":");
        const int lsb = constant(*parse_expr());
        ts_.expect("]");
        if (lsb != 0 || msb < 0 || msb > 63) ts_.fail("only [N-1:0] ranges up to 64 bits are supported");
        return msb + 1;
    }

    int constant(const Expr& e) const {
        Env env(m_.parameters.begin(), m_.parameters.end());
        return static_cast<int>(evaluate(e, env).bits);
    }

    void parse_parameters() {
        ts_.next();  // parameter / localparam
        const bool has_range = ts_.peek().text == "[";
        const int width = parse_optional_range();
        do {
            const std::string name = ts_.expect_identifier();
            ts_.expect("=");
            Env env(m_.parameters.begin(), m_.parameters.end());
            Value v = evaluate(*parse_expr(), env);
            if (has_range) v = Value::known(v.bits, width);
            m_.parameters[name] = v;
        } while (ts_.accept(","));
        ts_.expect(";");
    }

    void parse_always() {
        const std::string keyword = ts_.next().text;
        std::vector<std::string> edges;  // signals listed with posedge/negedge
        if (keyword != "always_comb") {
            ts_.expect("@");
            if (!ts_.accept("*")) {
                ts_.expect("(");
                while (!ts_.accept(")")) {
                    if (ts_.accept("posedge") || ts_.accept("negedge")) {
                        edges.push_back(ts_.expect_identifier());
                    } else {
                        ts_.next();
                    }
                }
            }
        }
        StmtPtr body = parse_statement();
        if (edges.empty()) {
            m_.comb_blocks.push_back(std::move(body));
        } else {
            m_.clock = edges[0];
            read_state_register(unwrap(*body));
        }
    }

    // Recognises: if (rst) S <= RESET; else S <= NEXT;
    void read_state_register(const Stmt& s) {
        const char* const kShape = "clocked block must be 'if (rst) state <= RESET; else state <= next;'";
        if (s.kind != Stmt::Kind::If || s.condition->kind != Expr::Kind::Identifier || !s.else_branch) {
            ts_.fail(kShape);
        }
        const Stmt& on_reset = unwrap(*s.then_branch);
        const Stmt& on_clock = unwrap(*s.else_branch);
        if (on_reset.kind != Stmt::Kind::Assign || on_clock.kind != Stmt::Kind::Assign ||
            on_reset.target != on_clock.target || on_clock.value->kind != Expr::Kind::Identifier) {
            ts_.fail(kShape);
        }
        m_.reset = s.condition->name;
        m_.state_reg = on_reset.target;
        m_.next_state_reg = on_clock.value->name;
        Env env(m_.parameters.begin(), m_.parameters.end());
        m_.reset_value = evaluate(*on_reset.value, env);
    }

    // Assertions are read separately by the SVA parser; here we only skip them.
    void skip_assertion() {
        if (ts_.peek(1).text == ":") {
            ts_.next();
            ts_.next();
        }
        if (ts_.accept("property")) {
            while (!ts_.accept("endproperty")) {
                if (ts_.at_end()) ts_.fail("missing endproperty");
                ts_.next();
            }
            return;
        }
        skip_to_semicolon();
        if (ts_.accept("else")) skip_to_semicolon();
    }

    void skip_to_semicolon() {
        int depth = 0;
        for (;;) {
            if (ts_.at_end()) ts_.fail("missing ';'");
            const std::string text = ts_.next().text;
            if (text == "(") ++depth;
            if (text == ")") --depth;
            if (text == ";" && depth == 0) return;
        }
    }

    StmtPtr parse_statement() {
        auto stmt = std::make_unique<Stmt>();
        if (ts_.accept("begin")) {
            if (ts_.accept(":")) ts_.expect_identifier();
            stmt->kind = Stmt::Kind::Block;
            while (!ts_.accept("end")) {
                if (ts_.at_end()) ts_.fail("missing 'end'");
                stmt->body.push_back(parse_statement());
            }
        } else if (ts_.accept("if")) {
            stmt->kind = Stmt::Kind::If;
            ts_.expect("(");
            stmt->condition = parse_expr();
            ts_.expect(")");
            stmt->then_branch = parse_statement();
            if (ts_.accept("else")) stmt->else_branch = parse_statement();
        } else if (ts_.peek().text == "case" || ts_.peek().text == "casex" ||
                   ts_.peek().text == "casez") {
            stmt->kind = Stmt::Kind::Case;
            stmt->case_keyword = ts_.next().text;
            ts_.expect("(");
            stmt->selector = parse_expr();
            ts_.expect(")");
            while (!ts_.accept("endcase")) {
                if (ts_.at_end()) ts_.fail("missing 'endcase'");
                CaseItem item;
                if (ts_.accept("default")) {
                    ts_.accept(":");
                } else {
                    do {
                        item.labels.push_back(parse_expr());
                    } while (ts_.accept(","));
                    ts_.expect(":");
                }
                item.body = parse_statement();
                stmt->items.push_back(std::move(item));
            }
        } else if (ts_.accept(";")) {
            stmt->kind = Stmt::Kind::Block;  // empty statement
        } else {
            stmt->kind = Stmt::Kind::Assign;
            stmt->target = ts_.expect_identifier();
            if (ts_.accept("[")) {
                stmt->target_bit = parse_expr();
                ts_.expect("]");
            }
            if (!ts_.accept("=") && !ts_.accept("<=")) ts_.fail("expected '=' or '<='");
            stmt->value = parse_expr();
            ts_.expect(";");
        }
        return stmt;
    }

    // Expression grammar: ternary > binary (precedence climbing) > unary > primary.
    ExprPtr parse_expr() {
        ExprPtr cond = parse_binary(1);
        if (!ts_.accept("?")) return cond;
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Ternary;
        e->args.push_back(std::move(cond));
        e->args.push_back(parse_expr());
        ts_.expect(":");
        e->args.push_back(parse_expr());
        return e;
    }

    ExprPtr parse_binary(int min_precedence) {
        ExprPtr lhs = parse_unary();
        for (;;) {
            const Token& tok = ts_.peek();
            const int prec = tok.kind == TokenKind::Symbol ? binary_precedence(tok.text) : -1;
            if (prec < min_precedence) return lhs;
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Binary;
            e->op = ts_.next().text;
            e->args.push_back(std::move(lhs));
            e->args.push_back(parse_binary(prec + 1));
            lhs = std::move(e);
        }
    }

    ExprPtr parse_unary() {
        const std::string& op = ts_.peek().text;
        if (ts_.peek().kind == TokenKind::Symbol && (op == "!" || op == "~" || op == "-")) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Unary;
            e->op = ts_.next().text;
            e->args.push_back(parse_unary());
            return e;
        }
        return parse_primary();
    }

    ExprPtr parse_primary() {
        auto e = std::make_unique<Expr>();
        if (ts_.peek().kind == TokenKind::Number) {
            e->kind = Expr::Kind::Literal;
            e->literal = parse_number(ts_.next().text);
        } else if (ts_.accept("(")) {
            e = parse_expr();
            ts_.expect(")");
        } else {
            e->name = ts_.expect_identifier();
            e->kind = Expr::Kind::Identifier;
            if (ts_.accept("[")) {
                e->kind = Expr::Kind::BitSelect;
                e->args.push_back(parse_expr());
                ts_.expect("]");
            }
        }
        return e;
    }

    void finish() {
        if (m_.state_reg.empty()) ts_.fail("no clocked always block found");
        if (!m_.regs.count(m_.state_reg) || !m_.regs.count(m_.next_state_reg)) {
            ts_.fail("state registers must be declared as reg");
        }
        auto is_control = [&](const Port& p) { return p.name == m_.clock || p.name == m_.reset; };
        m_.inputs.erase(std::remove_if(m_.inputs.begin(), m_.inputs.end(), is_control), m_.inputs.end());
        if (m_.comb_blocks.empty()) ts_.fail("no combinational always block found");
    }

    TokenStream ts_;
    FsmModule m_;
};

}  // namespace

FsmModule parse_fsm(const std::string& source) { return FsmParser(source).parse(); }

}  // namespace sva

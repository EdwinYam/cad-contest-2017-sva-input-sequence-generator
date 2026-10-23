#include "assertion.h"

namespace sva {

bool Condition::holds(char prev, char cur) const {
    switch (change) {
        case Change::Rose:
            return prev == '0' && cur == '1';
        case Change::Fell:
            return prev == '1' && cur == '0';
        case Change::Stable:
            return prev != '?' && prev == cur;
        case Change::High:
            return cur == '1';
        case Change::Low:
            return cur == '0';
    }
    return false;
}

bool Condition::holds_on(BitChange property, char cur) const {
    switch (change) {
        case Change::Rose:
            return property == BitChange::Rise;
        case Change::Fell:
            return property == BitChange::Fall;
        case Change::Stable:
            return property == BitChange::Stable;
        case Change::High:
            return cur == '1';
        case Change::Low:
            return cur == '0';
    }
    return false;
}

std::string Condition::str() const {
    switch (change) {
        case Change::Rose:
            return "$rose(" + bit.text + ")";
        case Change::Fell:
            return "$fell(" + bit.text + ")";
        case Change::Stable:
            return "$stable(" + bit.text + ")";
        case Change::High:
            return bit.text;
        case Change::Low:
            return "!" + bit.text;
    }
    return "";
}

std::string Assertion::str() const {
    return antecedent.str() + " |-> ##[" + std::to_string(a) + ":" + std::to_string(b) + "] " +
           consequent.str();
}

SignalTable::SignalTable(const FsmModule& fsm) {
    int offset = 0;
    for (const Port& p : fsm.inputs) {
        entries_[p.name] = {true, offset, p.width};
        offset += p.width;
    }
    offset = 0;
    for (const Port& p : fsm.outputs) {
        entries_[p.name] = {false, offset, p.width};
        offset += p.width;
    }
}

SignalBit SignalTable::resolve(const std::string& name, int index) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) throw ParseError("assertion uses unknown signal '" + name + "'");
    const Entry& e = it->second;
    SignalBit bit;
    bit.is_input = e.is_input;
    bit.text = index < 0 ? name : name + "[" + std::to_string(index) + "]";
    if (index < 0) {
        if (e.width != 1) throw ParseError("assertion must select one bit of '" + name + "'");
        index = 0;
    }
    if (index >= e.width) throw ParseError("bit index out of range in '" + bit.text + "'");
    bit.pos = e.offset + (e.width - 1 - index);  // strings are MSB first
    return bit;
}

namespace {

class SvaParser {
public:
    SvaParser(const std::string& source, const SignalTable& signals)
        : ts_(tokenize(source)), signals_(signals) {}

    std::vector<Assertion> parse() {
        std::vector<Assertion> out;
        while (!ts_.at_end()) {
            std::string label;
            if (ts_.peek().kind == TokenKind::Identifier && ts_.peek(1).text == ":" &&
                ts_.peek(2).text == "assert") {
                label = ts_.next().text;
                ts_.next();
            }
            if (ts_.peek().text == "assert" && ts_.peek(1).text == "property") {
                Assertion a = parse_assertion();
                a.name = label.empty() ? "assertion_" + std::to_string(out.size() + 1) : label;
                out.push_back(a);
            } else {
                ts_.next();
            }
        }
        return out;
    }

private:
    Assertion parse_assertion() {
        Assertion a;
        ts_.expect("assert");
        ts_.expect("property");
        ts_.expect("(");
        ts_.expect("@");
        ts_.expect("(");
        ts_.expect("posedge");
        ts_.expect_identifier();
        ts_.expect(")");

        a.antecedent = parse_condition();
        if (ts_.accept("|=>")) {
            a.a = a.b = 1;
        } else {
            ts_.expect("|->");
        }
        if (ts_.accept("##")) {
            if (ts_.accept("[")) {
                const int lo = ts_.expect_int();
                ts_.expect(":");
                if (ts_.peek().text == "$") ts_.fail("unbounded ranges ##[a:$] are not supported");
                const int hi = ts_.expect_int();
                ts_.expect("]");
                if (hi < lo) ts_.fail("empty delay range");
                a.a += lo;
                a.b += hi;
            } else {
                const int n = ts_.expect_int();
                a.a += n;
                a.b += n;
            }
        }
        a.consequent = parse_condition();
        ts_.expect(")");

        // Optional action block: else $display("..."); -- skipped.
        if (ts_.accept("else")) {
            while (!ts_.accept(";")) {
                if (ts_.at_end()) ts_.fail("missing ';' after assertion");
                ts_.next();
            }
        } else {
            ts_.expect(";");
        }
        return a;
    }

    Condition parse_condition() {
        Condition c;
        const std::string& word = ts_.peek().text;
        if (ts_.accept("(")) {
            c = parse_condition();
            ts_.expect(")");
        } else if (word == "$rose" || word == "$fell" || word == "$stable") {
            const std::string fn = ts_.next().text;
            c.change = fn == "$rose" ? Change::Rose : fn == "$fell" ? Change::Fell : Change::Stable;
            ts_.expect("(");
            c.bit = parse_signal();
            ts_.expect(")");
        } else if (ts_.accept("!") || ts_.accept("~")) {
            c.change = Change::Low;
            c.bit = parse_signal();
        } else {
            c.change = Change::High;
            c.bit = parse_signal();
        }
        return c;
    }

    SignalBit parse_signal() {
        const std::string name = ts_.expect_identifier();
        int index = -1;
        if (ts_.accept("[")) {
            index = ts_.expect_int();
            ts_.expect("]");
        }
        try {
            return signals_.resolve(name, index);
        } catch (const ParseError& e) {
            ts_.fail(e.what());
        }
    }

    TokenStream ts_;
    const SignalTable& signals_;
};

}  // namespace

std::vector<Assertion> parse_assertions(const std::string& source, const SignalTable& signals) {
    return SvaParser(source, signals).parse();
}

bool dominates(const Assertion& x, const Assertion& y) {
    return x.antecedent == y.antecedent && x.consequent == y.consequent && x.a <= y.a && x.b >= y.b;
}

}  // namespace sva

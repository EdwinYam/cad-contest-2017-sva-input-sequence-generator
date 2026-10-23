// Concurrent SVA rules of the form
//
//     [label:] assert property (@(posedge clk) A |-> ##[a:b] C) [else ...];
//
// where A and C are single-bit conditions: $rose(x), $fell(x), $stable(x),
// a level x, or !x. Also accepted: |=> (same as |-> ##1), ##n, and no delay.
// The rule fails when A holds at cycle T and C holds at none of T+a .. T+b.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "verilog_parser.h"

namespace sva {

// How an output bit changes along a Moore edge (the report's Edge::property).
enum class BitChange : unsigned char { Stable, Rise, Fall };

enum class Change { Rose, Fell, Stable, High, Low };

// One bit of the input vector or of the output vector. `pos` indexes the
// MSB-first bit string used everywhere (input lines, node outputs).
struct SignalBit {
    bool is_input = false;
    int pos = 0;
    std::string text;  // as written in the source, e.g. "in[1]"

    bool same_bit(const SignalBit& other) const {
        return is_input == other.is_input && pos == other.pos;
    }
};

struct Condition {
    Change change = Change::High;
    SignalBit bit;

    // Evaluates the condition from the bit's previous and current value
    // ('0', '1', or '?' when unknown -- an unknown value never satisfies it).
    bool holds(char prev, char cur) const;
    // Same, for an output bit, using a Moore edge's precomputed property.
    bool holds_on(BitChange property, char cur) const;

    bool operator==(const Condition& other) const {
        return change == other.change && bit.same_bit(other.bit);
    }
    std::string str() const;
};

struct Assertion {
    std::string name;
    Condition antecedent;
    Condition consequent;
    int a = 0;  // window start, in cycles after the trigger
    int b = 0;  // window end (the report's "termination time")

    std::string str() const;
};

// Maps port names to positions in the input/output bit strings.
class SignalTable {
public:
    explicit SignalTable(const FsmModule& fsm);
    // index < 0 means the whole (1-bit) signal.
    SignalBit resolve(const std::string& name, int index) const;

private:
    struct Entry {
        bool is_input;
        int offset;
        int width;
    };
    std::map<std::string, Entry> entries_;
};

// Finds every "assert property" in `source` (an fsm.v or a separate .sva file).
std::vector<Assertion> parse_assertions(const std::string& source, const SignalTable& signals);

// x dominates y when they watch the same trigger and consequent and x's window
// contains y's: any sequence that fails x also fails y (report, Sec. IV-B).
bool dominates(const Assertion& x, const Assertion& y);

}  // namespace sva

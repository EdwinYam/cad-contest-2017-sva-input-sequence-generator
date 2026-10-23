// Per-assertion input sequence generation (report, Sec. III-C).
//
// The search runs on the Moore graph. Its states are (Moore node, values of
// the input bits the assertion looks at in the last cycle); the extra input
// bits are needed because $rose(in[k]) depends on two consecutive inputs,
// which a Moore node alone does not record.
#pragma once

#include <string>
#include <vector>

#include "assertion.h"
#include "graph.h"

namespace sva {

struct SequenceResult {
    bool found = false;
    // A reset line followed by one "0 in[msb..lsb]" line per cycle.
    std::vector<std::string> lines;
    // Cycles after reset at which the antecedent fires (the report's k).
    int trigger_cycle = -1;
};

// Shortest reset-anchored sequence that fails `assertion`, or found=false
// when no reachable trigger can be followed by a failing window.
SequenceResult generate_one_sequence(const Graph& moore, const Assertion& assertion);

std::string reset_line(int input_width);  // "1000..."
std::string idle_line(int input_width);   // "0000..."

}  // namespace sva

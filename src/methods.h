// The three sequence-building schemes compared in the report (Sec. V).
//
//   A: one sequence per assertion, each started by a reset, concatenated.
//   B: like A, but skip assertions that an earlier piece already fails.
//   C: like B, but visit assertions by priority (largest termination time b
//      first) and skip assertions dominated by one that already failed.
#pragma once

#include <string>
#include <vector>

#include "assertion.h"
#include "graph.h"

namespace sva {

enum class Method { A, B, C };

struct GenerationResult {
    std::vector<std::string> sequence;  // full input sequence, one line per cycle
    std::vector<bool> failed;           // per assertion, from simulating `sequence`
    int sequences_generated = 0;        // calls to generate_one_sequence that succeeded
    int proven_unfailable = 0;          // assertions with no failing sequence at all
};

GenerationResult run_method(Method method, const Graph& mealy, const Graph& moore,
                            const std::vector<Assertion>& assertions);

// Method C's visiting order: b descending, then a ascending (so a dominating
// assertion comes before the ones it dominates), stable otherwise.
std::vector<size_t> priority_order(const std::vector<Assertion>& assertions);

}  // namespace sva

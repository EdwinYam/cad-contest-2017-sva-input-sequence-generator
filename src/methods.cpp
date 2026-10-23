#include "methods.h"

#include <algorithm>
#include <numeric>

#include "reachability.h"

namespace sva {

std::vector<size_t> priority_order(const std::vector<Assertion>& assertions) {
    // The report sorted with insertion sort; std::stable_sort gives the same
    // order in O(n log n).
    std::vector<size_t> order(assertions.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
        if (assertions[x].b != assertions[y].b) return assertions[x].b > assertions[y].b;
        return assertions[x].a < assertions[y].a;
    });
    return order;
}

GenerationResult run_method(Method method, const Graph& mealy, const Graph& moore,
                            const std::vector<Assertion>& assertions) {
    GenerationResult result;
    // The contest test bench needs rst to go 0 -> 1, so start with one idle line.
    result.sequence.push_back(idle_line(mealy.input_width));

    std::vector<size_t> order(assertions.size());
    std::iota(order.begin(), order.end(), 0);
    if (method == Method::C) order = priority_order(assertions);

    // Assertions already known to fail; only methods B and C consult it.
    std::vector<bool> known_failed(assertions.size(), false);

    for (size_t i : order) {
        if (method != Method::A && known_failed[i]) continue;
        const SequenceResult piece = generate_one_sequence(moore, assertions[i]);
        if (!piece.found) {
            ++result.proven_unfailable;
            continue;
        }
        ++result.sequences_generated;
        result.sequence.insert(result.sequence.end(), piece.lines.begin(), piece.lines.end());
        if (method == Method::A) continue;

        known_failed[i] = true;
        if (method == Method::C) {
            // Dominance: failing assertion i over its window also fails every
            // assertion with the same conditions and a window inside it.
            for (size_t j = 0; j < assertions.size(); ++j) {
                if (!known_failed[j] && dominates(assertions[i], assertions[j])) known_failed[j] = true;
            }
        }

        // Which other pending assertions does this piece fail as a side effect?
        // Each piece starts with a reset, so it can be simulated on its own.
        std::vector<size_t> pending;
        std::vector<Assertion> pending_rules;
        for (size_t j = 0; j < assertions.size(); ++j) {
            if (known_failed[j]) continue;
            pending.push_back(j);
            pending_rules.push_back(assertions[j]);
        }
        const std::vector<bool> hit = mealy.verify_assertion_failure(pending_rules, piece.lines);
        for (size_t k = 0; k < pending.size(); ++k) {
            if (hit[k]) known_failed[pending[k]] = true;
        }
    }

    // The reported count always comes from simulating the whole sequence.
    result.failed = mealy.verify_assertion_failure(assertions, result.sequence);
    return result;
}

}  // namespace sva

#include <gtest/gtest.h>

#include "left_edge.h"
#include "methods.h"
#include "reachability.h"
#include "test_util.h"

namespace sva {
namespace {

std::vector<std::string> with_idle(const Graph& g, const std::vector<std::string>& lines) {
    std::vector<std::string> seq{idle_line(g.input_width)};
    seq.insert(seq.end(), lines.begin(), lines.end());
    return seq;
}

TEST(GenerateOneSequence, ReproducesOfficialExample) {
    const test::Bench b = test::load_bench("contest_example");
    const SequenceResult r = generate_one_sequence(b.moore, b.assertions[0]);
    ASSERT_TRUE(r.found);
    // Reset, raise in[1], then three cycles without a rise of out (cf. Fig. 4).
    const std::vector<std::string> expected = {"100", "010", "000", "000", "000"};
    EXPECT_EQ(r.lines, expected);
    EXPECT_EQ(r.trigger_cycle, 1);
}

TEST(GenerateOneSequence, ProvesUnfailableRules) {
    const test::Bench b = test::load_bench("contest_example");
    EXPECT_FALSE(generate_one_sequence(b.moore, b.assertions[2]).found);  // rule3
}

// Every generated piece really fails its own assertion in simulation.
TEST(GenerateOneSequence, PieceFailsItsAssertion) {
    for (const std::string& name : test::bench_names()) {
        const test::Bench b = test::load_bench(name);
        for (const Assertion& a : b.assertions) {
            const SequenceResult r = generate_one_sequence(b.moore, a);
            if (!r.found) continue;
            EXPECT_EQ(r.lines.size(), static_cast<size_t>(1 + r.trigger_cycle + a.b)) << name << " " << a.name;
            EXPECT_TRUE(b.mealy.verify_assertion_failure({a}, with_idle(b.mealy, r.lines))[0])
                << name << " " << a.name;
        }
    }
}

// Exhaustive check on the 2-input example: the generated piece is as short as
// the shortest failing reset-anchored sequence, and "not found" means no
// sequence of up to 7 cycles fails the rule.
TEST(GenerateOneSequence, MatchesBruteForceOptimum) {
    const test::Bench b = test::load_bench("contest_example");
    const int max_cycles = 7;
    for (const Assertion& a : b.assertions) {
        int shortest = -1;
        for (int len = 1; len <= max_cycles && shortest < 0; ++len) {
            for (int code = 0; code < (1 << (2 * len)) && shortest < 0; ++code) {
                std::vector<std::string> seq = {"000", "100"};
                for (int t = 0; t < len; ++t) {
                    const int in = (code >> (2 * t)) & 3;
                    seq.push_back(std::string("0") + ((in & 2) ? '1' : '0') + ((in & 1) ? '1' : '0'));
                }
                if (b.mealy.verify_assertion_failure({a}, seq)[0]) shortest = len;
            }
        }
        const SequenceResult r = generate_one_sequence(b.moore, a);
        if (shortest < 0) {
            EXPECT_FALSE(r.found) << a.name;
        } else {
            ASSERT_TRUE(r.found) << a.name;
            EXPECT_EQ(static_cast<int>(r.lines.size()) - 1, shortest) << a.name;
        }
    }
}

TEST(GenerateOneSequence, HandlesInputConsequentAndZeroDelay) {
    const test::Bench b = test::load_bench("contest_example");
    const SignalTable signals(b.fsm);
    const auto rules = parse_assertions(R"(
        assert property (@(posedge clk) $rose(out) |-> $stable(in[0]));
        assert property (@(posedge clk) $rose(in[1]) |-> ##[0:2] $fell(in[1]));
    )", signals);
    for (const Assertion& a : rules) {
        const SequenceResult r = generate_one_sequence(b.moore, a);
        ASSERT_TRUE(r.found) << a.str();
        EXPECT_TRUE(b.mealy.verify_assertion_failure({a}, with_idle(b.mealy, r.lines))[0]) << a.str();
    }
}

TEST(Methods, PriorityOrder) {
    const test::Bench b = test::load_bench("contest_example");
    // All have a = 1; b is 3, 2, 1, 1, 2. Ties keep the input order.
    const std::vector<size_t> order = priority_order(b.assertions);
    const std::vector<size_t> expected = {0, 1, 4, 2, 3};
    EXPECT_EQ(order, expected);
}

TEST(Methods, AllMethodsFailEveryFailableAssertion) {
    for (const std::string& name : test::bench_names()) {
        const test::Bench b = test::load_bench(name);
        const GenerationResult ra = run_method(Method::A, b.mealy, b.moore, b.assertions);
        const GenerationResult rb = run_method(Method::B, b.mealy, b.moore, b.assertions);
        const GenerationResult rc = run_method(Method::C, b.mealy, b.moore, b.assertions);

        const int failable = static_cast<int>(b.assertions.size()) - ra.proven_unfailable;
        for (const GenerationResult* r : {&ra, &rb, &rc}) {
            int failed = 0;
            for (bool f : r->failed) failed += f;
            EXPECT_EQ(failed, failable) << name;
            EXPECT_EQ(r->sequence.front(), idle_line(b.mealy.input_width)) << name;
            EXPECT_EQ(r->sequence[1], reset_line(b.mealy.input_width)) << name;
        }
        EXPECT_LE(rb.sequence.size(), ra.sequence.size()) << name;
        EXPECT_LE(rc.sequence.size(), ra.sequence.size()) << name;
        EXPECT_LE(rb.sequences_generated, ra.sequences_generated) << name;
        EXPECT_LE(rc.sequences_generated, ra.sequences_generated) << name;
    }
}

TEST(Methods, MethodCSkipsDominatedAssertion) {
    const test::Bench b = test::load_bench("contest_example");
    const GenerationResult rc = run_method(Method::C, b.mealy, b.moore, b.assertions);
    // rule1's piece covers rule5 (dominance); rule2's piece also fails rule4
    // (in[0] rises together with out, and out does not rise again next cycle).
    EXPECT_EQ(rc.sequences_generated, 2);
    EXPECT_TRUE(rc.failed[3]);
    EXPECT_TRUE(rc.failed[4]);
}

TEST(LeftEdge, CompatibilityRules) {
    const test::Bench b = test::load_bench("contest_example");
    const Assertion& rule1 = b.assertions[0];  // $rose(in[1]) -> $rose(out)
    const Assertion& rule2 = b.assertions[1];  // $rose(out)   -> $fell(out)
    // rule2's $rose(out) trigger could fulfil rule1's $rose(out) consequent.
    EXPECT_FALSE(compatible(rule1, 1, rule2, 2));
    // ...unless their intervals [k, k+b] do not overlap.
    EXPECT_TRUE(compatible(rule1, 1, rule2, 5));
    // rule1's trigger is on in[1], rule4 watches out: no interference.
    EXPECT_TRUE(compatible(rule1, 1, b.assertions[3], 1));
}

TEST(LeftEdge, AssignsTracks) {
    std::vector<Net> nets = {{{0}, 0, 3, 0}, {{1}, 4, 6, 0}, {{2}, 2, 5, 0}, {{3}, 7, 9, 0}};
    EXPECT_EQ(assign_tracks_left_edge(nets), 2);
    EXPECT_EQ(nets[0].track, 1);
    EXPECT_EQ(nets[1].track, 1);
    EXPECT_EQ(nets[2].track, 2);
    EXPECT_EQ(nets[3].track, 1);
}

}  // namespace
}  // namespace sva

#include <gtest/gtest.h>

#include <random>

#include "test_util.h"

namespace sva {
namespace {

// B is entered with out = in, so mealy2moore must split it in two.
const char* const kSplitFsm = R"(
module fsm(clk, rst, in, out);
input clk, rst;
input in;
output reg out;
parameter A = 1'b0, B = 1'b1;
reg pstate, nstate;
always @(posedge clk or posedge rst) if (rst) pstate <= A; else pstate <= nstate;
always @(*) begin
  case (pstate)
    A: begin nstate = B; out = in; end
    B: begin nstate = A; out = 0; end
  endcase
end
endmodule
)";

// The second casex item is shadowed by the first; state 2 is unreachable.
const char* const kPriorityFsm = R"(
module fsm(clk, rst, in, out);
input clk, rst;
input [1:0] in;
output reg out;
reg [1:0] pstate, nstate;
always @(posedge clk or posedge rst) if (rst) pstate <= 0; else pstate <= nstate;
always @(*) begin
  nstate = pstate; out = 0;
  if (pstate == 0)
    casex (in)
      2'b1x: begin nstate = 1; out = 1; end
      2'b11: begin nstate = 2; out = 1; end
      2'b01: nstate = 3;
    endcase
  else nstate = 0;
end
endmodule
)";

const Edge* edge_to(const Node* from, const std::string& to) {
    for (const Edge* e : from->out_edges) {
        if (e->tail->name == to) return e;
    }
    return nullptr;
}

TEST(BuildMealy, ContestExample) {
    const test::Bench b = test::load_bench("contest_example");
    EXPECT_EQ(b.mealy.node_list().size(), 3U);
    EXPECT_EQ(b.mealy.edge_list().size(), 6U);
    EXPECT_EQ(b.mealy.initial->name, "S0");
    EXPECT_EQ(b.mealy.reset_output, "0");

    const Edge* stay = edge_to(b.mealy.initial, "S0");
    const Edge* go = edge_to(b.mealy.initial, "S1");
    ASSERT_TRUE(stay && go);
    EXPECT_EQ(stay->input, std::vector<std::string>{"0x"});
    EXPECT_EQ(go->input, std::vector<std::string>{"1x"});
    EXPECT_EQ(go->output, "0");

    const Edge* rise = edge_to(test::find_node(b.mealy, "S1"), "S2");
    ASSERT_TRUE(rise);
    EXPECT_EQ(rise->input, std::vector<std::string>{"x1"});
    EXPECT_EQ(rise->output, "1");
}

TEST(BuildMealy, CasexPriorityAndReachability) {
    const test::Bench b = test::make_bench(kPriorityFsm);
    EXPECT_EQ(b.mealy.node_list().size(), 3U);  // S0, S1, S3 -- never S2
    EXPECT_EQ(test::find_node(b.mealy, "S2"), nullptr);
    const Edge* e = edge_to(b.mealy.initial, "S1");
    ASSERT_TRUE(e);
    EXPECT_EQ(e->input, std::vector<std::string>{"1x"});
    EXPECT_TRUE(e->accepts("11"));
}

TEST(BuildMealy, IfElseStyle) {
    const test::Bench b = test::load_bench("handshake");
    // Ports: req gnt abort last. From IDLE, req=1 goes to WAIT whatever else.
    const Edge* e = edge_to(b.mealy.initial, "WAIT");
    ASSERT_TRUE(e);
    EXPECT_EQ(e->input, std::vector<std::string>{"1xxx"});
    EXPECT_EQ(e->output, "0000");
    EXPECT_EQ(b.mealy.node_list().size(), 6U);
}

TEST(Mealy2Moore, SplitsStatesByIncomingOutput) {
    const test::Bench b = test::make_bench(kSplitFsm);
    ASSERT_EQ(b.mealy.node_list().size(), 2U);
    ASSERT_EQ(b.moore.node_list().size(), 3U);  // A/0, B/0, B/1
    EXPECT_EQ(b.moore.edge_list().size(), 4U);
    EXPECT_EQ(b.moore.initial->name, "A/0");

    const Node* b1 = test::find_node(b.moore, "B/1");
    ASSERT_TRUE(b1);
    ASSERT_EQ(b1->in_edges.size(), 1U);
    EXPECT_EQ(b1->in_edges[0]->property[0], BitChange::Rise);
    ASSERT_EQ(b1->out_edges.size(), 1U);
    EXPECT_EQ(b1->out_edges[0]->property[0], BitChange::Fall);
    EXPECT_EQ(test::find_node(b.moore, "B/0")->in_edges[0]->property[0], BitChange::Stable);
}

TEST(Mealy2Moore, PropertyMatchesHeadAndTailOutputs) {
    for (const std::string& name : test::bench_names()) {
        const test::Bench b = test::load_bench(name);
        for (const auto& e : b.moore.edge_list()) {
            for (size_t i = 0; i < e->property.size(); ++i) {
                const char from = e->head->output[i];
                const char to = e->tail->output[i];
                const BitChange want = from == to ? BitChange::Stable : to == '1' ? BitChange::Rise
                                                                                  : BitChange::Fall;
                ASSERT_EQ(e->property[i], want) << name;
            }
        }
    }
}

// Random input sequences produce the same outputs on both machines.
TEST(Mealy2Moore, SameBehaviourAsMealy) {
    std::mt19937 rng(7);
    for (const std::string& name : test::bench_names()) {
        const test::Bench b = test::load_bench(name);
        for (int run = 0; run < 50; ++run) {
            const Node* m = b.mealy.initial;
            const Node* n = b.moore.initial;
            ASSERT_EQ(n->output, b.mealy.reset_output);
            for (int t = 0; t < 40; ++t) {
                std::string in;
                for (int i = 0; i < b.mealy.input_width; ++i) in += (rng() & 1U) ? '1' : '0';
                const Edge* me = nullptr;
                for (const Edge* e : m->out_edges) me = e->accepts(in) ? e : me;
                const Edge* ne = nullptr;
                for (const Edge* e : n->out_edges) ne = e->accepts(in) ? e : ne;
                ASSERT_TRUE(me && ne) << name;
                ASSERT_EQ(me->output, ne->tail->output) << name;
                m = me->tail;
                n = ne->tail;
                ASSERT_EQ(n->mealy_id, m->id) << name;
            }
        }
    }
}

TEST(Graph, ReachableDistances) {
    test::Bench b = test::load_bench("contest_example");
    EXPECT_EQ(b.mealy.compute_reachable_dist(), 3);
    EXPECT_EQ(test::find_node(b.mealy, "S2")->reachable_dist, 2U);
}

class VerifyTest : public ::testing::Test {
protected:
    VerifyTest() : b_(test::load_bench("contest_example")) {}
    bool fails(const std::vector<std::string>& seq, size_t rule = 0) {
        return b_.mealy.verify_assertion_failure(b_.assertions, seq).at(rule);
    }
    test::Bench b_;
};

// Figure 4 of the problem statement: rule1 fails.
TEST_F(VerifyTest, OfficialExampleSequenceFails) {
    EXPECT_TRUE(fails({"000", "100", "010", "000", "000", "000", "001"}));
}

TEST_F(VerifyTest, RiseInsideWindowPasses) {
    EXPECT_FALSE(fails({"000", "100", "010", "001", "001", "001", "001"}));
}

TEST_F(VerifyTest, WindowMustEndInsideSequence) {
    EXPECT_FALSE(fails({"000", "100", "010", "000", "000"}));
    EXPECT_TRUE(fails({"000", "100", "010", "000", "000", "000"}));
}

TEST_F(VerifyTest, UnknownStateBeforeResetNeverFails) {
    // in[1] rises before any reset: outputs are X, so nothing is decided.
    EXPECT_FALSE(fails({"000", "010", "000", "000", "000", "000"}));
}

TEST_F(VerifyTest, ChecksContinueAcrossReset) {
    // The trigger window spans a reset; out never rises, so rule1 fails.
    EXPECT_TRUE(fails({"000", "100", "010", "100", "100", "000"}));
}

}  // namespace
}  // namespace sva

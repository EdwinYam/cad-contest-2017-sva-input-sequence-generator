#include <gtest/gtest.h>

#include "test_util.h"

namespace sva {
namespace {

const char* const kAnsiFsm = R"(
module fsm(input clk, input rst, input [2:0] in, output reg [1:0] out);
  localparam IDLE = 0, RUN = 1;
  reg pstate, nstate;
  always @(posedge clk or posedge rst)
    if (rst) pstate <= IDLE; else pstate <= nstate;
  always @* begin
    nstate = pstate;
    out = 2'b00;
    if (pstate == IDLE && in[2]) begin nstate = RUN; out[1] = 1; end
    else if (pstate == RUN) out = in[0] == 0 ? 2'b11 : 2'b01;
  end
endmodule
)";

TEST(Lexer, SplitsSvaOperators) {
    std::vector<std::string> texts;
    for (const Token& t : tokenize("$rose(in[1])|->##[1:3] $fell(out) // c\n/* x */ |=>")) {
        texts.push_back(t.text);
    }
    const std::vector<std::string> expected = {"$rose", "(", "in", "[", "1", "]", ")", "|->", "##", "[",
                                               "1", ":", "3", "]", "$fell", "(", "out", ")", "|=>", ""};
    EXPECT_EQ(texts, expected);
}

TEST(Lexer, ParsesNumberLiterals) {
    const Value v = parse_number("2'b0x");
    EXPECT_EQ(v.width, 2);
    EXPECT_EQ(v.bits, 0U);
    EXPECT_EQ(v.care, 0b10U);

    EXPECT_EQ(parse_number("8'hA_5").bits, 0xA5U);
    EXPECT_EQ(parse_number("3'd6").bits, 6U);
    EXPECT_EQ(parse_number("42").bits, 42U);
    EXPECT_EQ(parse_number("4'b1??0").care, 0b1001U);
    EXPECT_THROW(parse_number("2'b2"), ParseError);
}

TEST(VerilogAst, CaseMatchingRules) {
    const Value sel = Value::known(0b10, 2);
    EXPECT_TRUE(case_matches("casex", sel, parse_number("2'b1x")));
    EXPECT_FALSE(case_matches("casex", sel, parse_number("2'b0x")));
    EXPECT_FALSE(case_matches("case", sel, parse_number("2'b1x")));  // x never matches in case
    EXPECT_TRUE(case_matches("case", sel, parse_number("2'b10")));
}

TEST(VerilogParser, ReadsContestExample) {
    const FsmModule m = parse_fsm(test::read_file(std::string(SVA_GEN_SOURCE_DIR) +
                                                  "/benchmarks/contest_example/fsm.v"));
    EXPECT_EQ(m.name, "fsm");
    EXPECT_EQ(m.clock, "clk");
    EXPECT_EQ(m.reset, "rst");
    ASSERT_EQ(m.inputs.size(), 1U);
    EXPECT_EQ(m.inputs[0].name, "in");
    EXPECT_EQ(m.inputs[0].width, 2);
    ASSERT_EQ(m.outputs.size(), 1U);
    EXPECT_EQ(m.outputs[0].width, 1);
    EXPECT_EQ(m.state_reg, "pstate");
    EXPECT_EQ(m.next_state_reg, "nstate");
    EXPECT_EQ(m.reset_value.bits, 0U);
    EXPECT_EQ(m.parameters.size(), 3U);
    EXPECT_EQ(m.comb_blocks.size(), 1U);
}

TEST(VerilogParser, ReadsAnsiHeaderIfElseAndTernary) {
    const FsmModule m = parse_fsm(kAnsiFsm);
    ASSERT_EQ(m.inputs.size(), 1U);
    EXPECT_EQ(m.inputs[0].width, 3);
    ASSERT_EQ(m.outputs.size(), 1U);
    EXPECT_EQ(m.outputs[0].width, 2);
    EXPECT_EQ(m.state_reg, "pstate");
}

TEST(VerilogParser, RejectsUnsupportedClockedBlock) {
    const char* src = R"(
module fsm(clk, rst, in, out);
input clk, rst, in; output reg out; reg s, n;
always @(posedge clk) s <= n;
always @(*) begin n = s; out = in; end
endmodule)";
    EXPECT_THROW(parse_fsm(src), ParseError);
}

class SvaParserTest : public ::testing::Test {
protected:
    SvaParserTest() : fsm_(parse_fsm(kAnsiFsm)), signals_(fsm_) {}

    Assertion parse_one(const std::string& body) {
        const auto rules = parse_assertions("assert property (@(posedge clk) " + body + ");", signals_);
        EXPECT_EQ(rules.size(), 1U);
        return rules.at(0);
    }

    FsmModule fsm_;
    SignalTable signals_;
};

TEST_F(SvaParserTest, RangeDelay) {
    const Assertion a = parse_one("$rose(in[2]) |-> ##[1:3] $fell(out[0])");
    EXPECT_EQ(a.antecedent.change, Change::Rose);
    EXPECT_TRUE(a.antecedent.bit.is_input);
    EXPECT_EQ(a.antecedent.bit.pos, 0);  // in[2] is the MSB of a 3-bit input
    EXPECT_EQ(a.consequent.change, Change::Fell);
    EXPECT_FALSE(a.consequent.bit.is_input);
    EXPECT_EQ(a.consequent.bit.pos, 1);
    EXPECT_EQ(a.a, 1);
    EXPECT_EQ(a.b, 3);
}

TEST_F(SvaParserTest, OtherForms) {
    Assertion a = parse_one("$fell(out[1]) |=> $stable(in[0])");
    EXPECT_EQ(a.a, 1);
    EXPECT_EQ(a.b, 1);
    EXPECT_EQ(a.consequent.change, Change::Stable);

    a = parse_one("in[1] |-> ##2 !out[1]");
    EXPECT_EQ(a.antecedent.change, Change::High);
    EXPECT_EQ(a.consequent.change, Change::Low);
    EXPECT_EQ(a.a, 2);
    EXPECT_EQ(a.b, 2);

    a = parse_one("$rose(in[0]) |-> $rose(out[0])");
    EXPECT_EQ(a.a, 0);
    EXPECT_EQ(a.b, 0);

    a = parse_one("$rose(in[0]) |=> ##[0:2] $rose(out[0])");
    EXPECT_EQ(a.a, 1);
    EXPECT_EQ(a.b, 3);
}

TEST_F(SvaParserTest, LabelsAndActionBlocks) {
    const auto rules = parse_assertions(R"(
        first: assert property (@(posedge clk) $rose(in[0]) |-> ##1 $rose(out[0]))
               else $display("first failed; really");
        assert property (@(posedge clk) $fell(in[0]) |-> ##1 $fell(out[0]));
    )", signals_);
    ASSERT_EQ(rules.size(), 2U);
    EXPECT_EQ(rules[0].name, "first");
    EXPECT_EQ(rules[1].name, "assertion_2");
}

TEST_F(SvaParserTest, RejectsBadSignals) {
    const std::string pre = "assert property (@(posedge clk) ";
    EXPECT_THROW(parse_assertions(pre + "$rose(foo) |-> $rose(out[0]));", signals_), ParseError);
    EXPECT_THROW(parse_assertions(pre + "$rose(in) |-> $rose(out[0]));", signals_), ParseError);
    EXPECT_THROW(parse_assertions(pre + "$rose(in[3]) |-> $rose(out[0]));", signals_), ParseError);
    EXPECT_THROW(parse_assertions(pre + "$rose(in[0]) |-> ##[1:$] $rose(out[0]));", signals_),
                 ParseError);
}

TEST(SvaParser, ReadsAssertionsEmbeddedInFsm) {
    const test::Bench b = test::load_bench("contest_example");
    ASSERT_EQ(b.assertions.size(), 5U);
    EXPECT_EQ(b.assertions[0].name, "assertion_rule1");
    EXPECT_EQ(b.assertions[0].str(), "$rose(in[1]) |-> ##[1:3] $rose(out)");
}

TEST(SvaParser, Dominance) {
    const test::Bench b = test::load_bench("contest_example");
    const Assertion& rule1 = b.assertions[0];  // [1:3]
    const Assertion& rule5 = b.assertions[4];  // [1:2], same conditions
    EXPECT_TRUE(dominates(rule1, rule5));
    EXPECT_FALSE(dominates(rule5, rule1));
    EXPECT_FALSE(dominates(rule1, b.assertions[3]));  // different trigger
}

}  // namespace
}  // namespace sva

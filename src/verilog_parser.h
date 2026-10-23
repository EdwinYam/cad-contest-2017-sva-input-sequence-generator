// Parser for the FSM subset of Verilog used by the contest benchmarks.
//
// Supported:
//   * module header in plain or ANSI style; input/output/reg/wire declarations
//     with [N-1:0] ranges; parameter/localparam constants
//   * one clocked block of the form
//       always @(posedge clk or posedge rst) if (rst) S <= RESET; else S <= N;
//   * any number of combinational blocks (always @(*) / always_comb) built
//     from assignments, if/else and case/casex/casez
//   * assertion statements are skipped here and read by parse_assertions()
#pragma once

#include <map>
#include <string>
#include <vector>

#include "verilog_ast.h"

namespace sva {

struct Port {
    std::string name;
    int width = 1;
};

struct FsmModule {
    std::string name;
    std::string clock;
    std::string reset;
    std::vector<Port> inputs;   // data inputs in declaration order (clock/reset removed)
    std::vector<Port> outputs;  // outputs in declaration order
    std::map<std::string, Port> regs;
    std::map<std::string, Value> parameters;
    std::string state_reg;       // present-state register
    std::string next_state_reg;  // value loaded into state_reg when not in reset
    Value reset_value;
    std::vector<StmtPtr> comb_blocks;
};

FsmModule parse_fsm(const std::string& source);

}  // namespace sva

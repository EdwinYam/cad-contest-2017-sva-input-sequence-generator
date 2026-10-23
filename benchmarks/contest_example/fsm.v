// The example FSM from the contest problem statement, with the assertions
// embedded in the module as in the contest benchmarks.
module fsm(clk, rst, in, out);
input clk, rst;
input [1:0] in;
output out;

parameter S0 = 2'b00,
          S1 = 2'b01,
          S2 = 2'b10;
reg [1:0] pstate, nstate;
reg out;

always @(posedge clk or posedge rst) begin
    if (rst) pstate <= S0;
    else pstate <= nstate;
end

always @(*) begin
    case (pstate)
        S0: casex (in)
                2'b0x: begin nstate = S0; out = 0; end
                2'b1x: begin nstate = S1; out = 0; end
            endcase
        S1: casex (in)
                2'bx0: begin nstate = S1; out = 0; end
                2'bx1: begin nstate = S2; out = 1; end
            endcase
        S2: casex (in)
                2'bx1: begin nstate = S2; out = 1; end
                2'bx0: begin nstate = S0; out = 0; end
            endcase
    endcase
end

// From the problem statement.
assertion_rule1: assert property
    (@(posedge clk) $rose(in[1]) |-> ##[1:3] $rose(out))
    else $display("assertion rule1 failed");
// out stays high for at least two cycles when in[0] stays high.
assertion_rule2: assert property
    (@(posedge clk) $rose(out) |-> ##[1:2] $fell(out))
    else $display("assertion rule2 failed");
// Cannot fail: after out falls the FSM is in S0, whose output is always 0.
assertion_rule3: assert property
    (@(posedge clk) $fell(out) |-> ##1 !out)
    else $display("assertion rule3 failed");
assertion_rule4: assert property
    (@(posedge clk) $rose(in[0]) |=> $rose(out))
    else $display("assertion rule4 failed");
// Dominated by rule1: same conditions, narrower window.
assertion_rule5: assert property
    (@(posedge clk) $rose(in[1]) |-> ##[1:2] $rose(out))
    else $display("assertion rule5 failed");

endmodule

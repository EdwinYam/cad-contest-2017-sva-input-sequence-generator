// Two-way traffic light with an emergency override, written with casex on a
// packed input vector and a packed output vector.
//   in  = {emergency, car_ew, car_ns}
//   out = {ns_g, ns_y, ns_r, ew_g, ew_y, ew_r}
module fsm(clk, rst, in, out);
input clk, rst;
input [2:0] in;
output [5:0] out;

parameter NSG0 = 4'd0, NSG1 = 4'd1, NSG2 = 4'd2, NSY = 4'd3,
          EWG0 = 4'd4, EWG1 = 4'd5, EWG2 = 4'd6, EWY = 4'd7, RED = 4'd8;
reg [3:0] pstate, nstate;
reg [5:0] out;

always @(posedge clk or posedge rst) begin
    if (rst) pstate <= RED;
    else pstate <= nstate;
end

always @(*) begin
    case (pstate)
        NSG0: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            default: begin nstate = NSG1; out = 6'b100001; end
        endcase
        NSG1: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            3'b01x:  begin nstate = NSY;  out = 6'b100001; end
            default: begin nstate = NSG2; out = 6'b100001; end
        endcase
        NSG2: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            default: begin nstate = NSY;  out = 6'b100001; end
        endcase
        NSY: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            default: begin nstate = EWG0; out = 6'b010001; end
        endcase
        EWG0: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            default: begin nstate = EWG1; out = 6'b001100; end
        endcase
        EWG1: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            3'b0x1:  begin nstate = EWY;  out = 6'b001100; end
            default: begin nstate = EWG2; out = 6'b001100; end
        endcase
        EWG2: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            default: begin nstate = EWY;  out = 6'b001100; end
        endcase
        EWY: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            default: begin nstate = NSG0; out = 6'b001010; end
        endcase
        default: casex (in)
            3'b1xx:  begin nstate = RED;  out = 6'b001001; end
            3'b010:  begin nstate = EWG0; out = 6'b001001; end
            default: begin nstate = NSG0; out = 6'b001001; end
        endcase
    endcase
end

endmodule

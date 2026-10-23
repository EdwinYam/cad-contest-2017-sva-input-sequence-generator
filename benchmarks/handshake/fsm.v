// Request/grant transfer controller written with if/else inside a case on
// the present state, separate 1-bit ports, and default assignments.
module fsm(clk, rst, req, gnt, abort, last, ack, busy, err, done);
input clk, rst;
input req, gnt, abort, last;
output reg ack, busy, err, done;

parameter IDLE = 3'd0, WAIT = 3'd1, XFER = 3'd2, DONE = 3'd3, ERR = 3'd4, HOLD = 3'd5;
reg [2:0] pstate, nstate;

always @(posedge clk or posedge rst) begin
    if (rst) pstate <= IDLE;
    else pstate <= nstate;
end

always @(*) begin
    nstate = pstate;
    ack = 0; busy = 0; err = 0; done = 0;
    case (pstate)
        IDLE: if (req) nstate = WAIT;
        WAIT: begin
            busy = 1;
            if (abort) begin nstate = ERR; err = 1; end
            else if (gnt) begin nstate = XFER; ack = 1; end
        end
        XFER: begin
            busy = 1; ack = 1;
            if (abort) begin nstate = ERR; err = 1; ack = 0; end
            else if (last) begin nstate = DONE; done = 1; end
        end
        DONE: begin
            done = 1;
            if (!req) nstate = IDLE;
            else nstate = HOLD;
        end
        HOLD: begin
            busy = 1;
            if (!req) nstate = IDLE;
        end
        ERR: begin
            err = 1;
            if (!req && !abort) nstate = IDLE;
        end
        default: nstate = IDLE;
    endcase
end

endmodule

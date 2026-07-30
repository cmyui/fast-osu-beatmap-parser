// Find first set bit: position of the lowest set bit in a mask.
//
// The textbook isolate-lowest trick is `mask & -mask`, one subtract and one
// AND, but a WIDTH-bit borrow chain is a WIDTH-deep dependency. A prefix-OR
// gets the same answer in log2(WIDTH) levels: bit i is the first set bit
// exactly when it is set and nothing below it is.
//
// Portable Verilog (no SV size casts) so Yosys can synthesize it.

`default_nettype none

module fosu_ffs #(
    parameter int WIDTH = 64
) (
    input  wire  [WIDTH-1:0]         mask,
    output logic [$clog2(WIDTH)-1:0] pos,
    output logic                     found
);
    localparam int PW  = $clog2(WIDTH);
    localparam int LOG = $clog2(WIDTH);

    // Inclusive prefix OR: po[LOG][i] = |mask[i:0].
    logic [WIDTH-1:0] po [0:LOG];
    always_comb begin
        integer i, st, d;
        po[0] = mask;
        for (st = 1; st <= LOG; st = st + 1) begin
            d = 1 << (st - 1);
            for (i = 0; i < WIDTH; i = i + 1)
                if (i >= d) po[st][i] = po[st-1][i] | po[st-1][i-d];
                else        po[st][i] = po[st-1][i];
        end
    end

    // Lowest set bit: set here, and nothing strictly below.
    logic [WIDTH-1:0] onehot;
    always_comb begin
        integer i;
        onehot[0] = mask[0];
        for (i = 1; i < WIDTH; i = i + 1)
            onehot[i] = mask[i] && !po[LOG][i-1];
    end

    always_comb begin
        integer i;
        logic [31:0] acc;
        acc = 32'd0;
        for (i = 0; i < WIDTH; i = i + 1)
            if (onehot[i]) acc = acc | i;
        pos   = acc[PW-1:0];
        found = |mask;
    end

endmodule

`default_nettype wire

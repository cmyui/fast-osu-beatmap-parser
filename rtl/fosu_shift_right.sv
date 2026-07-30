// Variable right shift of a WIDTH-bit mask by 0..WIDTH-1, as a barrel shifter:
// log2(WIDTH) stages, each conditionally shifting by a power of two.
//
// Needed because run lengths inside a field start at a position the parser only
// discovers at runtime (the fractional digits begin after a '.' whose offset
// depends on the integer part). Shifting the mask down to that position turns
// "count consecutive digits starting at k" into "count consecutive digits
// starting at 0", which is a fixed-position problem.

`default_nettype none

module fosu_shift_right #(
    parameter int WIDTH = 64
) (
    input  wire [WIDTH-1:0]         din,
    input  wire [$clog2(WIDTH)-1:0] amt,
    output logic [WIDTH-1:0]        dout
);
    localparam int LOG = $clog2(WIDTH);

    logic [WIDTH-1:0] stage [0:LOG];
    always_comb begin
        integer st, sh;
        stage[0] = din;
        for (st = 1; st <= LOG; st = st + 1) begin
            sh = 1 << (st - 1);
            if (amt[st-1]) stage[st] = stage[st-1] >> sh;
            else           stage[st] = stage[st-1];
        end
    end

    always_comb dout = stage[LOG];

endmodule

`default_nettype wire

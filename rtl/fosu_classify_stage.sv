// One pipeline stage: bytes in, masks out, one clock of latency.
//
// This is a single position on the conveyor belt. Every flip-flop below
// samples its input on every rising clock edge -- nothing "arrives" at them
// and nothing dispatches to them; the clock is a metronome saying "commit".
// Between edges, fosu_classify's LUTs ripple and settle; the edge captures
// whatever they have settled to.
//
// valid_in/valid_out is the standard stream tag: it travels alongside the
// data through every stage so consumers know which cycles carry real bytes.
// The data registers are deliberately NOT reset -- their contents are
// don't-care while valid_out is low, and leaving them out of the reset
// network saves routing on a wide datapath.

`default_nettype none

module fosu_classify_stage #(
    parameter int WIDTH = 32
) (
    input  wire                   clk,
    input  wire                   rst_n,

    input  wire                   valid_in,
    input  wire [8*WIDTH-1:0]     bytes_in,

    output logic                  valid_out,
    output logic [WIDTH-1:0]      nondigit_mask,
    output logic [WIDTH-1:0]      comma_mask,
    output logic [WIDTH-1:0]      newline_mask
);

    wire [WIDTH-1:0] nd, cm, nl;

    fosu_classify #(.WIDTH(WIDTH)) u_classify (
        .bytes_in      (bytes_in),
        .nondigit_mask (nd),
        .comma_mask    (cm),
        .newline_mask  (nl)
    );

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            valid_out <= 1'b0;
        end else begin
            valid_out <= valid_in;
        end

        nondigit_mask <= nd;
        comma_mask    <= cm;
        newline_mask  <= nl;
    end

endmodule

`default_nettype wire

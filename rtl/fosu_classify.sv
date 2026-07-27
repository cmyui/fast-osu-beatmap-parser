// Byte classification: the hardware form of fosu's AVX2 mask generation.
//
// The C++ (hitobject_prefix.hpp) needs a bias trick to test a byte range --
// add 80, then signed-compare -- because AVX2 has no unsigned byte compare.
// Hardware has no such constraint: the range test is written literally, and
// synthesis maps each lane to a couple of LUTs whose truth tables hold the
// answer for all 256 possible input bytes.
//
// Purely combinational: no clock, no state. Given bytes, the masks settle
// after one LUT propagation delay. The registered wrapper that turns this
// into a pipeline stage is fosu_classify_stage.sv.

`default_nettype none

module fosu_classify #(
    // Bytes classified per cycle. 32 mirrors the AVX2 window the C++ prefix
    // parser loads; the streaming ingest bus width is a separate decision.
    parameter int WIDTH = 32
) (
    input  wire [8*WIDTH-1:0] bytes_in,

    // Bit i corresponds to byte i. nondigit_mask is inverted-sense to match
    // the C++ nondigit_mask32() exactly, so the golden-model diff is literal.
    output wire [WIDTH-1:0]   nondigit_mask,
    output wire [WIDTH-1:0]   comma_mask,
    output wire [WIDTH-1:0]   newline_mask
);

    // One classifier per byte lane -- WIDTH physical copies, every one of them
    // evaluating every cycle. Where software loops over bytes or issues one
    // vector instruction, hardware simply owns WIDTH instances of the logic.
    // Each lane is wired to its own 8 bits of the input and sees nothing else.
    for (genvar i = 0; i < WIDTH; i++) begin : g_lane
        wire [7:0] b = bytes_in[8*i +: 8];

        assign nondigit_mask[i] = (b < 8'd48) || (b > 8'd57);  // not '0'..'9'
        assign comma_mask[i]    = (b == 8'd44);                // ','
        assign newline_mask[i]  = (b == 8'd10);                // '\n'
    end

endmodule

`default_nettype wire

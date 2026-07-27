// Teaching example: AND two 32-bit numbers, three ways.
//
// The same trivial operation built at three levels of interface, because the
// interesting part of hardware is never the arithmetic -- it is how a circuit
// talks to whatever is feeding it. Read these in order.
//
// Nothing here is part of the parser; see the `rtl-example` make target.

`default_nettype none

// Real RTL keeps one module per file, and the filename must match -- which is
// why the parser's modules live in their own files. Waived here on purpose:
// reading the three levels side by side is the entire point of this file.
/* verilator lint_off DECLFILENAME */

// ---------------------------------------------------------------------------
// LEVEL 1 -- combinational. There is no call and no return.
//
// This is not a function that runs. It is 32 AND gates that exist and are
// powered, permanently. `y` is *continuously* the AND of whatever voltages a
// and b are currently showing: change one input bit and y's corresponding bit
// follows a fraction of a nanosecond later, with nothing triggering it.
//
// No clock appears below, and that is not an omission -- this circuit has no
// notion of time or sequence at all.
// ---------------------------------------------------------------------------
module and32_comb (
    input  wire [31:0] a,
    input  wire [31:0] b,
    output wire [31:0] y
);
    // 32 independent one-bit lanes, side by side. Bit 7 of the result depends
    // only on bit 7 of each input -- no bit here can see any other bit.
    assign y = a & b;
endmodule


// ---------------------------------------------------------------------------
// LEVEL 2 -- registered. "Return" becomes a tagged stream.
//
// Same gates, now with a row of flip-flops after them. The result appears one
// clock edge later, so we have introduced *latency*: 1 cycle. What we have NOT
// given up is *throughput* -- a fresh pair can be pushed in on every single
// cycle, and results emerge one per cycle behind them. Latency and throughput
// are independent axes in hardware, and conflating them is the most common
// software intuition to unlearn.
//
// `in_valid`/`out_valid` is how a result is "returned": not a value handed
// back to a caller, but a tag travelling alongside the data telling whoever is
// downstream "this cycle's bits are real."
// ---------------------------------------------------------------------------
module and32_pipe (
    input  wire         clk,
    input  wire         rst_n,

    input  wire         in_valid,
    input  wire  [31:0] a,
    input  wire  [31:0] b,

    output logic        out_valid,
    output logic [31:0] y
);
    always_ff @(posedge clk) begin
        // Every flip-flop below samples on EVERY rising edge, unconditionally.
        // Nothing "arrives" to wake them up.
        if (!rst_n) out_valid <= 1'b0;
        else        out_valid <= in_valid;

        // Deliberately not reset and not gated by in_valid: garbage in an
        // invalid cycle is harmless because out_valid says to ignore it, and
        // leaving these 32 bits out of the reset network saves routing.
        y <= a & b;
    end
endmodule


// ---------------------------------------------------------------------------
// LEVEL 3 -- valid/ready handshake. This is the actual calling convention.
//
// Level 2 assumes the consumer can always take a result immediately. Real
// systems stall: a FIFO fills, a DMA engine is mid-burst. So each side gets a
// wire, and the contract is exactly one rule:
//
//     data transfers on any cycle where valid AND ready are both high.
//
// `valid` flows downstream ("I have data"), `ready` flows upstream ("I can
// take data") -- that upstream wire is backpressure, and it is the closest
// thing hardware has to a blocking call. This is the AXI-Stream contract that
// every block in a real FPGA design speaks, including our parser's ingest bus.
// ---------------------------------------------------------------------------
module and32_stream (
    input  wire         clk,
    input  wire         rst_n,

    // upstream (producer) side
    input  wire         in_valid,
    output wire         in_ready,
    input  wire  [31:0] a,
    input  wire  [31:0] b,

    // downstream (consumer) side
    output logic        out_valid,
    input  wire         out_ready,
    output logic [31:0] y
);
    // We hold exactly one result, so we can accept new work whenever we are
    // either empty, or being drained this very cycle.
    assign in_ready = !out_valid || out_ready;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            out_valid <= 1'b0;
        end else if (in_valid && in_ready) begin
            // Accept. Note this also covers simultaneous drain-and-fill: if
            // the consumer is taking the old y on this same edge, overwriting
            // it is correct.
            y         <= a & b;
            out_valid <= 1'b1;
        end else if (out_valid && out_ready) begin
            // Drained with nothing new offered: go empty.
            out_valid <= 1'b0;
        end
        // Otherwise hold everything. A stalled stage is a stage whose
        // flip-flops keep re-capturing their own current values.
    end
endmodule


// ---------------------------------------------------------------------------
// Wrapper so one Verilator build can exercise all three at once. A real design
// would never bundle unrelated blocks like this.
// ---------------------------------------------------------------------------
module and32_demo (
    input  wire         clk,
    input  wire         rst_n,

    // level 1
    input  wire  [31:0] c_a,
    input  wire  [31:0] c_b,
    output wire  [31:0] c_y,

    // level 2
    input  wire         p_in_valid,
    input  wire  [31:0] p_a,
    input  wire  [31:0] p_b,
    output wire         p_out_valid,
    output wire  [31:0] p_y,

    // level 3
    input  wire         s_in_valid,
    output wire         s_in_ready,
    input  wire  [31:0] s_a,
    input  wire  [31:0] s_b,
    output wire         s_out_valid,
    input  wire         s_out_ready,
    output wire  [31:0] s_y
);
    and32_comb u_comb (.a(c_a), .b(c_b), .y(c_y));

    and32_pipe u_pipe (
        .clk(clk), .rst_n(rst_n),
        .in_valid(p_in_valid), .a(p_a), .b(p_b),
        .out_valid(p_out_valid), .y(p_y)
    );

    and32_stream u_stream (
        .clk(clk), .rst_n(rst_n),
        .in_valid(s_in_valid), .in_ready(s_in_ready), .a(s_a), .b(s_b),
        .out_valid(s_out_valid), .out_ready(s_out_ready), .y(s_y)
    );
endmodule

`default_nettype wire

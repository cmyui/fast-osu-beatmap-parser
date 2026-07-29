// Positions of the first four set bits in a mask -- the hardware form of the
// C++ blsr/tzcnt chain.
//
// The C++ needs a trick here: a naive tzcnt/shift/repeat loop puts four
// dependent operations on the critical path, so fast_parse_prefix peels the
// low bits with a blsr chain (1 cycle each) to make all four tzcnts
// independent. That is a workaround for having exactly one scalar ALU chain.
//
// Hardware doesn't need it. All four positions come out of a single rank
// computation: rank[i] is how many set bits precede position i, so bit i is
// the k-th set bit exactly when mask[i] is 1 and rank[i] == k. Every position
// is then a one-hot select -- the four results are genuinely simultaneous
// rather than merely independent.
//
// Deliberately avoids SystemVerilog size casts and other constructs Yosys's
// Verilog frontend rejects: staying portable keeps `make rtl-stat` (area and
// depth measurement) and the open-source ASIC flow available, not just Vivado.

`default_nettype none

module fosu_first4 #(
    parameter int WIDTH = 32
) (
    input  wire  [WIDTH-1:0]         mask,

    // pos[k] = index of the (k+1)-th set bit; meaningful only if found[k].
    output logic [$clog2(WIDTH)-1:0] pos0,
    output logic [$clog2(WIDTH)-1:0] pos1,
    output logic [$clog2(WIDTH)-1:0] pos2,
    output logic [$clog2(WIDTH)-1:0] pos3,
    output logic [3:0]               found
);
    localparam int PW = $clog2(WIDTH);
    localparam int RW = $clog2(WIDTH + 1);

    // Exclusive prefix popcount, as a parallel-prefix (Kogge-Stone) network.
    //
    // The obvious way to write this is a running sum, rank[i] = rank[i-1] +
    // mask[i-1], which is correct and reads better -- but it builds a chain of
    // 31 dependent adders, and Yosys measured the resulting core at 135 gate
    // levels deep. Prefix summation is associative, so the same result comes
    // out of log2(WIDTH) = 5 stages instead: at stage k every position adds
    // the partial sum from 2^(k-1) positions back. Same gate count order,
    // logarithmic depth.
    //
    // This is the hardware version of the lesson the C++ campaign learned from
    // the blsr trick -- shortening the dependency chain matters more than the
    // operation count -- except here it is the dominant effect rather than a
    // few percent.
    localparam int LOG = $clog2(WIDTH);

    logic [RW-1:0] ps [0:LOG][0:WIDTH-1];
    always_comb begin
        integer i, st, d;
        for (i = 0; i < WIDTH; i = i + 1)
            ps[0][i] = {{(RW-1){1'b0}}, mask[i]};
        for (st = 1; st <= LOG; st = st + 1) begin
            d = 1 << (st - 1);
            for (i = 0; i < WIDTH; i = i + 1)
                if (i >= d) ps[st][i] = ps[st-1][i] + ps[st-1][i-d];
                else        ps[st][i] = ps[st-1][i];
        end
    end

    // Inclusive -> exclusive: the count strictly before position i.
    logic [RW-1:0] rank [0:WIDTH-1];
    always_comb begin
        integer i;
        rank[0] = {RW{1'b0}};
        for (i = 1; i < WIDTH; i = i + 1)
            rank[i] = ps[LOG][i-1];
    end

    // One-hot per rank k: the single position holding the k-th set bit.
    logic [WIDTH-1:0] sel [0:3];
    always_comb begin
        integer i, k;
        for (k = 0; k < 4; k = k + 1)
            for (i = 0; i < WIDTH; i = i + 1)
                // rank widened to 32 bits explicitly so the comparison against
                // the integer loop variable has matching operand widths.
                sel[k][i] = mask[i] && ({{(32-RW){1'b0}}, rank[i]} == k);
    end

    // One-hot to index. Exactly one bit of sel[k] can be set, so OR-ing the
    // indices of whichever bits are set yields that bit's index.
    function automatic [PW-1:0] onehot_to_idx(input logic [WIDTH-1:0] oh);
        logic [31:0] acc;
        integer i;
        begin
            acc = 32'd0;
            for (i = 0; i < WIDTH; i = i + 1)
                if (oh[i]) acc = acc | i;
            onehot_to_idx = acc[PW-1:0];
        end
    endfunction

    always_comb begin
        integer k;
        pos0 = onehot_to_idx(sel[0]);
        pos1 = onehot_to_idx(sel[1]);
        pos2 = onehot_to_idx(sel[2]);
        pos3 = onehot_to_idx(sel[3]);
        for (k = 0; k < 4; k = k + 1)
            found[k] = |sel[k];
    end

endmodule

`default_nettype wire

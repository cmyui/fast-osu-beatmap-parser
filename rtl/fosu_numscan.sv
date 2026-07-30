// The shared numeric scanner: one combinational pass over a byte window that
// serves every numeric field in the .osu format.
//
// The format has exactly two numeric shapes, and the C++ has one function for
// each: parse_i64/parse_u64 for integers and parse_double for decimals. Both
// are driven from the same bytes, so in hardware they share one scanner and the
// consumer picks which outputs it believes. That is the area-reuse form of the
// same instinct the C++ campaign followed with speculative conversion: compute
// both interpretations, decide afterwards.
//
// Two facts from the C++ make this tractable in a single window:
//
//   * parse_u64 accumulates only the first 19 digits but CONSUMES all of them,
//     so an integer's value is the value of its first min(n,19) digits no
//     matter how long the run is. Only the consume length can exceed a window,
//     and the caller loops for that.
//
//   * parse_double bails to strtod above 18 significant digits or on an
//     exponent. So any decimal it handles itself is at most sign + 18 digits +
//     '.', i.e. 20 bytes -- always inside one window. Longer ones only need to
//     be detected and skipped, not evaluated.
//
// So `mant`/`frac` here reproduce parse_double's inputs exactly, and the host
// finishes with the same two operations the C++ uses:
//     v = (double)mant;  if (frac) v /= kPow10[frac];  out = neg ? -v : v;
// which makes the result bit-identical rather than merely close. No floating
// point unit in silicon.

`default_nettype none

module fosu_numscan #(
    parameter int WIN = 64
) (
    input  wire [8*WIN-1:0] bytes_in,

    // --- shape ---
    output logic            neg,        // a leading '-'
    output logic [7:0]      nd_int,     // digits in the integer part
    output logic [7:0]      nd_frac,    // digits after '.' (0 if none)
    output logic            has_dot,
    output logic            has_exp,    // 'e'/'E' terminates the number
    output logic            int_any,    // digits present for the integer form
    output logic            dec_any,    // digits present for the decimal form

    // --- integer interpretation (parse_u64 / parse_i64) ---
    output logic [63:0]     val19,      // value of the first min(nd_int,19)
    output logic [7:0]      int_len,     // bytes consumed: sign + nd_int
    output logic            int_run_full, // run fills the window; may continue

    // --- decimal interpretation (parse_double) ---
    output logic [63:0]     mant,       // int+frac digits concatenated
    output logic [7:0]      frac,       // == nd_frac, the kPow10 divisor index
    output logic            needs_host, // >18 digits or an exponent: strtod
    output logic [7:0]      dec_len     // bytes consumed by the decimal form
);
    localparam int PW = $clog2(WIN);
    localparam logic [7:0] CH_MINUS = 8'h2D;
    localparam logic [7:0] CH_DOT   = 8'h2E;
    localparam logic [7:0] CH_ZERO  = 8'h30;
    localparam logic [7:0] CH_NINE  = 8'h39;
    localparam logic [7:0] CH_LE    = 8'h65;  // 'e'
    localparam logic [7:0] CH_UE    = 8'h45;  // 'E'

    logic [7:0] byte_at [0:WIN-1];
    logic [3:0] dig     [0:WIN-1];
    logic [WIN-1:0] digmask;
    always_comb begin
        integer i;
        for (i = 0; i < WIN; i = i + 1) begin
            byte_at[i] = bytes_in[8*i +: 8];
            dig[i]     = bytes_in[8*i +: 4];
            digmask[i] = (byte_at[i] >= CH_ZERO) && (byte_at[i] <= CH_NINE);
        end
    end

    // Reading a byte outside the window yields 0, which is neither a digit nor
    // a delimiter -- the same role the buffer padding plays for the C++.
    function automatic [7:0] b_at(input integer idx);
        if (idx < 0 || idx >= WIN) b_at = 8'h00;
        else                       b_at = byte_at[idx];
    endfunction

    function automatic [3:0] d_at(input integer idx);
        if (idx < 0 || idx >= WIN) d_at = 4'd0;
        else                       d_at = dig[idx];
    endfunction

    // ---------------------------------------------------------------------
    // Run lengths. "Consecutive digits starting at k" becomes "starting at 0"
    // by shifting the digit mask down to k, then finding the first zero.
    // ---------------------------------------------------------------------
    logic [7:0]      base;          // first digit position (past any sign)
    logic [7:0]      frac_start;    // first fractional digit position
    logic [PW-1:0]   sh_int_amt, sh_frac_amt;
    logic [WIN-1:0]  sh_int, sh_frac;

    always_comb begin
        neg  = (byte_at[0] == CH_MINUS);
        base = neg ? 8'd1 : 8'd0;
        sh_int_amt = base[PW-1:0];
    end

    fosu_shift_right #(.WIDTH(WIN)) u_sh_int (
        .din(digmask), .amt(sh_int_amt), .dout(sh_int));

    // First zero in the shifted mask == length of the leading run.
    logic [PW-1:0] z_int_pos, z_frac_pos;
    logic          z_int_found, z_frac_found;

    fosu_ffs #(.WIDTH(WIN)) u_z_int (
        .mask(~sh_int), .pos(z_int_pos), .found(z_int_found));

    always_comb begin
        // No zero anywhere means every remaining byte is a digit.
        nd_int = z_int_found ? {{(8-PW){1'b0}}, z_int_pos} : (WIN[7:0] - base);
        int_run_full = !z_int_found;   // the run reaches the window edge
        int_any      = (nd_int != 0);

        // NOT gated on int_any: parse_double accepts ".5" -- it tries the
        // integer run, finds nothing, then still consumes a '.' and the
        // fractional digits after it.
        has_dot     = (b_at({24'd0, base} + {24'd0, nd_int}) == CH_DOT);
        frac_start  = base + nd_int + 8'd1;
        sh_frac_amt = frac_start[PW-1:0];
    end

    fosu_shift_right #(.WIDTH(WIN)) u_sh_frac (
        .din(digmask), .amt(sh_frac_amt[PW-1:0]), .dout(sh_frac));

    fosu_ffs #(.WIDTH(WIN)) u_z_frac (
        .mask(~sh_frac), .pos(z_frac_pos), .found(z_frac_found));

    logic [7:0] nd_frac_raw;
    always_comb begin
        nd_frac_raw = z_frac_found ? {{(8-PW){1'b0}}, z_frac_pos}
                                   : (WIN[7:0] - frac_start);
        nd_frac     = has_dot ? nd_frac_raw : 8'd0;
        frac        = nd_frac;
        // parse_double's `any` flag spans both parts.
        dec_any     = (nd_int != 0) || (nd_frac != 0);
    end

    // ---------------------------------------------------------------------
    // Integer value: the first min(nd_int,19) digits, right-aligned.
    // ---------------------------------------------------------------------
    function automatic [63:0] pow10_64(input integer k);
        case (k)
            0:  pow10_64 = 64'd1;
            1:  pow10_64 = 64'd10;
            2:  pow10_64 = 64'd100;
            3:  pow10_64 = 64'd1000;
            4:  pow10_64 = 64'd10000;
            5:  pow10_64 = 64'd100000;
            6:  pow10_64 = 64'd1000000;
            7:  pow10_64 = 64'd10000000;
            8:  pow10_64 = 64'd100000000;
            9:  pow10_64 = 64'd1000000000;
            10: pow10_64 = 64'd10000000000;
            11: pow10_64 = 64'd100000000000;
            12: pow10_64 = 64'd1000000000000;
            13: pow10_64 = 64'd10000000000000;
            14: pow10_64 = 64'd100000000000000;
            15: pow10_64 = 64'd1000000000000000;
            16: pow10_64 = 64'd10000000000000000;
            17: pow10_64 = 64'd100000000000000000;
            18: pow10_64 = 64'd1000000000000000000;
            default: pow10_64 = 64'd0;
        endcase
    endfunction

    logic [7:0]  n_eff;                 // min(nd_int, 19)
    logic [63:0] iterm [0:18];
    always_comb begin
        integer k;
        n_eff = (nd_int > 8'd19) ? 8'd19 : nd_int;
        for (k = 0; k < 19; k = k + 1)
            if (k < {24'd0, n_eff})
                iterm[k] = {60'd0, d_at({24'd0, base} + {24'd0, n_eff} - 1 - k)}
                           * pow10_64(k);
            else
                iterm[k] = 64'd0;
    end

    // Balanced sum: 19 terms in 5 levels rather than a 19-deep accumulate.
    logic [63:0] ia [0:9], ib [0:4], ic [0:2], id [0:1];
    always_comb begin
        integer k;
        for (k = 0; k < 9; k = k + 1) ia[k] = iterm[2*k] + iterm[2*k+1];
        ia[9] = iterm[18];
        for (k = 0; k < 5; k = k + 1) ib[k] = ia[2*k] + ia[2*k+1];
        ib[0] = ia[0] + ia[1];
        ib[1] = ia[2] + ia[3];
        ib[2] = ia[4] + ia[5];
        ib[3] = ia[6] + ia[7];
        ib[4] = ia[8] + ia[9];
        ic[0] = ib[0] + ib[1];
        ic[1] = ib[2] + ib[3];
        ic[2] = ib[4];
        id[0] = ic[0] + ic[1];
        id[1] = ic[2];
        val19 = id[0] + id[1];
        // parse_i64 returns its start pointer when no digits follow the sign,
        // so a bare "-" or "abc" consumes nothing at all.
        int_len = int_any ? (base + nd_int) : 8'd0;
    end

    // ---------------------------------------------------------------------
    // Decimal mantissa: int and fractional digits concatenated, skipping the
    // '.', valid only when the total is at most 18 digits.
    // ---------------------------------------------------------------------
    logic [7:0]  total_dig;
    logic [63:0] mterm [0:17];

    // Logical digit j of the mantissa lives at byte j, or j+1 once past the dot.
    function automatic [3:0] mant_dig(input integer j);
        if (j < {24'd0, nd_int}) mant_dig = d_at({24'd0, base} + j);
        else                     mant_dig = d_at({24'd0, base} + j + 1);
    endfunction

    always_comb begin
        integer k;
        total_dig = nd_int + nd_frac;
        for (k = 0; k < 18; k = k + 1)
            if (k < {24'd0, total_dig})
                mterm[k] = {60'd0, mant_dig({24'd0, total_dig} - 1 - k)}
                           * pow10_64(k);
            else
                mterm[k] = 64'd0;
    end

    logic [63:0] ma [0:8], mb [0:4], mc [0:2], md [0:1];
    always_comb begin
        integer k;
        for (k = 0; k < 9; k = k + 1) ma[k] = mterm[2*k] + mterm[2*k+1];
        mb[0] = ma[0] + ma[1];
        mb[1] = ma[2] + ma[3];
        mb[2] = ma[4] + ma[5];
        mb[3] = ma[6] + ma[7];
        mb[4] = ma[8];
        mc[0] = mb[0] + mb[1];
        mc[1] = mb[2] + mb[3];
        mc[2] = mb[4];
        md[0] = mc[0] + mc[1];
        md[1] = mc[2];
        mant  = md[0] + md[1];
    end

    logic [7:0] end_pos;
    always_comb begin
        // The '.' itself is consumed even when no fractional digits follow it,
        // matching parse_double's unconditional ++p past the dot.
        end_pos = has_dot ? (base + nd_int + 8'd1 + nd_frac)
                          : (base + nd_int);
        has_exp = dec_any && ((b_at({24'd0, end_pos}) == CH_LE) ||
                              (b_at({24'd0, end_pos}) == CH_UE));
        // parse_double punts above 18 significant digits or on an exponent.
        needs_host = dec_any && ((total_dig > 8'd18) || has_exp);
        // With no digits at all, parse_double also returns its start pointer.
        dec_len    = dec_any ? end_pos : 8'd0;
    end

endmodule

`default_nettype wire

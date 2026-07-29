// Hitobject line prefix parser: "x,y,time,type,hitSound" -> integer fields.
// Combinational; a registered pipeline wrapper comes once the depth is
// measured (see `make rtl-stat`).
//
// This is the hardware counterpart of fast_parse_prefix() in
// include/fosu/hitobject_prefix.hpp, and it is deliberately SIMPLER than the
// C++ rather than a transliteration of it. The C++ carries a 64-byte-per-entry
// consteval table of vpermd + vpshufb masks, indexed by a mixed-radix length
// signature, whose entire job is to slide each variable-width field into a
// fixed lane so one maddubs/madd chain can convert everything at once. That
// machinery exists because AVX2 cannot shift each lane by its own amount.
//
// Hardware has no such restriction: given the delimiter positions, a digit at
// a computed index is just a mux. So the table, the index polynomial
// (60*p0 + 27*p1 + 2*p2 + p3 - 158), the permute and the shuffle all vanish,
// and what remains is the actual work -- select digits, weight them, add.
//
// Fields are extracted RIGHT-aligned (ones digit adjacent to the delimiter),
// which makes a variable-length field a fixed set of muxes with the unused
// high digits forced to zero. No normalization step is needed at all.
//
// Everything is computed speculatively and validated in parallel, exactly as
// the C++ does: `ok` is the single predicate saying whether the fast path's
// domain was met, and the field outputs are meaningless unless it is set.
//
// Portable Verilog (no SV size casts) so Yosys can synthesize it as well as
// Vivado -- see the note in fosu_first4.sv.

`default_nettype none

module fosu_prefix #(
    // Bytes classified for delimiter positions. 32 matches the C++ AVX2
    // window, so the accepted domain is identical.
    parameter int WINDOW = 32,
    // Total bytes presented. Tail reads (hitSound digits and the terminator)
    // can reach p3+3 and p3 <= WINDOW-1, so a few extra bytes keep every read
    // in range. This is the RTL's version of the kBufferPadding guarantee
    // io.hpp already makes to the C++ parser.
    parameter int BYTES = WINDOW + 8
) (
    input  wire  [8*BYTES-1:0] bytes_in,

    output logic               ok,          // fast path accepted this line
    output logic [31:0]        x,
    output logic [31:0]        y,
    output logic [31:0]        time_ms,
    output logic [31:0]        obj_type,
    output logic [31:0]        hitsound,
    output logic [7:0]         next_off,    // offset just past hitSound
    output logic [WINDOW-1:0]  newline_mask // line end, free from the same load
);
    localparam int PW = $clog2(WINDOW);
    localparam int IW = $clog2(BYTES);   // width of a byte-array index

    // Field-length limits, matching the C++ bounds checks exactly:
    //   x, y  1..3 digits   (census: >3 digits in 33 of 12.3M coords)
    //   time  1..10 digits  (census: real maps top out at 7)
    //   type  1..3 digits
    localparam logic [IW-1:0] MAX_XY_DIGITS   = 3;
    localparam logic [IW-1:0] MAX_TIME_DIGITS = 10;
    localparam logic [IW-1:0] MAX_TYPE_DIGITS = 3;

    localparam logic [7:0] CH_NL    = 8'h0A;
    localparam logic [7:0] CH_CR    = 8'h0D;
    localparam logic [7:0] CH_COMMA = 8'h2C;
    localparam logic [7:0] CH_ZERO  = 8'h30;
    localparam logic [7:0] CH_NINE  = 8'h39;

    // ---------------------------------------------------------------------
    // Byte views. `dig` is the digit value of each byte; garbage on
    // non-digits, which is harmless because those positions are never summed
    // on an accepted line.
    // ---------------------------------------------------------------------
    logic [7:0] byte_at [0:BYTES-1];
    logic [3:0] dig     [0:BYTES-1];
    always_comb begin
        integer i;
        for (i = 0; i < BYTES; i = i + 1) begin
            byte_at[i] = bytes_in[8*i +: 8];
            dig[i]     = bytes_in[8*i +: 4];  // ASCII '0'..'9' low nibble
        end
    end

    // ---------------------------------------------------------------------
    // Classify the window and locate the first four non-digits.
    // ---------------------------------------------------------------------
    logic [WINDOW-1:0] nondigit_mask, comma_mask;

    fosu_classify #(.WIDTH(WINDOW)) u_classify (
        .bytes_in      (bytes_in[8*WINDOW-1:0]),
        .nondigit_mask (nondigit_mask),
        .comma_mask    (comma_mask),
        .newline_mask  (newline_mask)
    );

    logic [PW-1:0] p0, p1, p2, p3;
    logic [3:0]    found;

    fosu_first4 #(.WIDTH(WINDOW)) u_first4 (
        .mask  (nondigit_mask),
        .pos0  (p0), .pos1(p1), .pos2(p2), .pos3(p3),
        .found (found)
    );

    // Widened positions, so index arithmetic like p3+3 cannot wrap at WINDOW.
    logic [IW-1:0] w0, w1, w2, w3;
    always_comb begin
        w0 = {{(IW-PW){1'b0}}, p0};
        w1 = {{(IW-PW){1'b0}}, p1};
        w2 = {{(IW-PW){1'b0}}, p2};
        w3 = {{(IW-PW){1'b0}}, p3};
    end

    // ---------------------------------------------------------------------
    // Field lengths. Positions are strictly ascending, so no underflow: a
    // zero length means two adjacent delimiters, which fails the range check
    // below just as the C++ unsigned-compare trick catches an empty field.
    // ---------------------------------------------------------------------
    logic [IW-1:0] len_x, len_y, len_t, len_ty;
    always_comb begin
        len_x  = w0;
        len_y  = w1 - w0 - {{(IW-1){1'b0}}, 1'b1};
        len_t  = w2 - w1 - {{(IW-1){1'b0}}, 1'b1};
        len_ty = w3 - w2 - {{(IW-1){1'b0}}, 1'b1};
    end

    // ---------------------------------------------------------------------
    // Digit selection. Reading right-to-left from a delimiter: the ones digit
    // sits at end-1, tens at end-2, and so on. Out-of-range indices return
    // zero so the speculative path can never read a bogus position.
    // ---------------------------------------------------------------------
    function automatic [3:0] dig_rev(input logic [IW-1:0] end_pos,
                                     input integer j);
        integer idx;
        begin
            idx = {{(32-IW){1'b0}}, end_pos};
            idx = idx - 1 - j;
            if (idx < 0 || idx >= BYTES) dig_rev = 4'd0;
            else                         dig_rev = dig[idx];
        end
    endfunction

    // A field of `len` digits ending at `end_pos`, up to 3 digits wide.
    function automatic [31:0] conv3(input logic [IW-1:0] end_pos,
                                    input logic [IW-1:0] len);
        logic [31:0] acc;
        begin
            acc = {28'd0, dig_rev(end_pos, 0)};
            if (len >= 2) acc = acc + {28'd0, dig_rev(end_pos, 1)} * 32'd10;
            if (len >= 3) acc = acc + {28'd0, dig_rev(end_pos, 2)} * 32'd100;
            conv3 = acc;
        end
    endfunction

    // Time is up to 10 digits. Summed as a BALANCED TREE, not an accumulator
    // loop: `acc = acc + term` ten times builds ten dependent 34-bit adds, and
    // Yosys measured that chain -- not the prefix network -- as the real
    // critical path. Pairwise summation is 4 levels instead of 10.
    function automatic [33:0] pow10(input integer j);
        case (j)
            0: pow10 = 34'd1;
            1: pow10 = 34'd10;
            2: pow10 = 34'd100;
            3: pow10 = 34'd1000;
            4: pow10 = 34'd10000;
            5: pow10 = 34'd100000;
            6: pow10 = 34'd1000000;
            7: pow10 = 34'd10000000;
            8: pow10 = 34'd100000000;
            9: pow10 = 34'd1000000000;
            default: pow10 = 34'd0;
        endcase
    endfunction

    logic [33:0] t_term [0:9];
    logic [33:0] t_a [0:4];
    logic [33:0] t_b [0:2];
    logic [33:0] t_c [0:1];
    logic [33:0] time_raw;

    always_comb begin
        integer j;
        // Each term: one digit mux, one constant multiply, zeroed past the
        // field length. All ten are independent.
        for (j = 0; j < MAX_TIME_DIGITS; j = j + 1)
            if (j < {{(32-IW){1'b0}}, len_t})
                t_term[j] = {30'd0, dig_rev(w2, j)} * pow10(j);
            else
                t_term[j] = 34'd0;

        t_a[0] = t_term[0] + t_term[1];
        t_a[1] = t_term[2] + t_term[3];
        t_a[2] = t_term[4] + t_term[5];
        t_a[3] = t_term[6] + t_term[7];
        t_a[4] = t_term[8] + t_term[9];

        t_b[0] = t_a[0] + t_a[1];
        t_b[1] = t_a[2] + t_a[3];
        t_b[2] = t_a[4];

        t_c[0] = t_b[0] + t_b[1];
        t_c[1] = t_b[2];

        time_raw = t_c[0] + t_c[1];
    end

    always_comb begin
        x        = conv3(w0, len_x);
        y        = conv3(w1, len_y);
        obj_type = conv3(w3, len_ty);
        time_ms  = time_raw[31:0];
    end

    // ---------------------------------------------------------------------
    // hitSound: one or two digits after the fourth comma, then a legal
    // terminator. Matches the C++ rule -- ',' CR LF or NUL (the padded end of
    // buffer) -- so the accepted domain is the same.
    //
    // Note CR/LF are accepted here because in a file they ARE the end of the
    // line. A field parser fed a body that still contains a newline is a
    // framing bug upstream, not something this module should tolerate.
    // ---------------------------------------------------------------------
    function automatic is_digit_b(input logic [7:0] b);
        is_digit_b = (b >= CH_ZERO) && (b <= CH_NINE);
    endfunction

    function automatic is_term_b(input logic [7:0] b);
        is_term_b = (b == CH_COMMA) || (b == CH_CR) || (b == CH_NL) ||
                    (b == 8'h00);
    endfunction

    logic [IW-1:0] i_hs0, i_hs1, i_hs2;   // p3+1, p3+2, p3+3
    logic [7:0] hs_b0, hs_b1, hs_after1, hs_after2;
    logic       hs_d0_ok, hs_d1_ok, hs_term_ok;

    always_comb begin
        // p3 <= WINDOW-1 and BYTES >= WINDOW+3, so these are always in range.
        i_hs0     = w3 + {{(IW-1){1'b0}}, 1'b1};
        i_hs1     = w3 + {{(IW-2){1'b0}}, 2'd2};
        i_hs2     = w3 + {{(IW-2){1'b0}}, 2'd3};
        hs_b0     = byte_at[i_hs0];
        hs_b1     = byte_at[i_hs1];
        hs_after1 = byte_at[i_hs1];   // terminator for a 1-digit value
        hs_after2 = byte_at[i_hs2];   // terminator for a 2-digit value

        hs_d0_ok = is_digit_b(hs_b0);
        hs_d1_ok = is_digit_b(hs_b1);

        if (hs_d1_ok) begin
            hitsound   = {28'd0, dig[i_hs0]} * 32'd10 + {28'd0, dig[i_hs1]};
            next_off   = {{(8-IW){1'b0}}, i_hs2};
            hs_term_ok = is_term_b(hs_after2);
        end else begin
            hitsound   = {28'd0, dig[i_hs0]};
            next_off   = {{(8-IW){1'b0}}, i_hs1};
            hs_term_ok = is_term_b(hs_after1);
        end
    end

    // ---------------------------------------------------------------------
    // The single accept predicate. Every term is computed in parallel with the
    // conversions above; nothing here gates the datapath, it only decides
    // whether to believe it.
    // ---------------------------------------------------------------------
    logic delims_are_commas, lengths_ok, time_fits;
    always_comb begin
        delims_are_commas = comma_mask[p0] && comma_mask[p1] &&
                            comma_mask[p2] && comma_mask[p3];

        lengths_ok = (len_x  != 0) && (len_x  <= MAX_XY_DIGITS)   &&
                     (len_y  != 0) && (len_y  <= MAX_XY_DIGITS)   &&
                     (len_t  != 0) && (len_t  <= MAX_TIME_DIGITS) &&
                     (len_ty != 0) && (len_ty <= MAX_TYPE_DIGITS);

        // int32 range. Only reachable with 10 digits, but checked
        // unconditionally -- in hardware the comparator costs area, not time,
        // and both outcomes already exist as wires.
        time_fits = (time_raw <= 34'd2147483647);

        ok = (found == 4'b1111) && lengths_ok && delims_are_commas &&
             time_fits && hs_d0_ok && hs_term_ok;
    end

endmodule

`default_nettype wire

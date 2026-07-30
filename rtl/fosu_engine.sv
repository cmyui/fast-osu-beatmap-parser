// The parser engine: line iteration composed with per-section content parsing.
//
// fosu_line_iter discovers lines and sections and offers them one at a time
// over a valid/ready handshake; this module takes as many cycles as a line
// needs, then releases it. The two share the single memory port -- while a line
// is being parsed the iterator is stalled, so its address outputs are stable
// and the engine steals the port for its own cursor.
//
// Currently implemented: [TimingPoints]. Other sections are accepted and
// released untouched, which keeps line iteration verified end to end while the
// remaining section parsers land.
//
// Every numeric field goes through the one shared fosu_numscan instance, at
// whatever offset the cursor currently sits. Values the C++ would hand to
// strtod (over 18 significant digits, or an exponent) are not approximated
// here: the line is emitted as a PUNT record carrying its span, and the host
// parses it. That is the same division of labour as the C++ scalar fallback,
// and it keeps the result bit-identical rather than nearly right.

`default_nettype none

module fosu_engine #(
    parameter int WIN = 64
) (
    input  wire         clk,
    input  wire         rst_n,

    input  wire         start,
    input  wire [31:0]  file_len,

    output logic [31:0] mem_addr,
    input  wire [8*WIN-1:0] mem_data,

    // Record stream.
    output logic        rec_valid,
    input  wire         rec_ready,
    output logic [3:0]  rec_tag,

    // TAG_TIMING payload. time/beat_length are emitted as parse_double's own
    // inputs so the host reproduces the identical double:
    //   v = (double)mant; if (frac) v /= kPow10[frac]; out = neg ? -v : v;
    output logic [63:0] tp_time_mant,
    output logic [7:0]  tp_time_frac,
    output logic        tp_time_neg,
    output logic [63:0] tp_bl_mant,
    output logic [7:0]  tp_bl_frac,
    output logic        tp_bl_neg,
    output logic [31:0] tp_meter,
    output logic [31:0] tp_sample_set,
    output logic [31:0] tp_sample_index,
    output logic [31:0] tp_volume,
    output logic        tp_uninherited,
    output logic [31:0] tp_effects,

    // TAG_PUNT / TAG_MALFORMED payload: the line span.
    output logic [31:0] rec_start,
    output logic [31:0] rec_len,
    output logic [3:0]  rec_section,

    output logic        done
);
    localparam logic [3:0] TAG_TIMING    = 4'd1;
    localparam logic [3:0] TAG_MALFORMED = 4'd2;
    localparam logic [3:0] TAG_PUNT      = 4'd3;

    localparam logic [3:0] SEC_TIMING = 4'd6;

    localparam logic [7:0] CH_COMMA = 8'h2C;

    // ---------------------------------------------------------------------
    // Line source.
    // ---------------------------------------------------------------------
    logic [31:0] li_addr;
    logic        li_valid, li_ready;
    logic [3:0]  li_section;
    logic [1:0]  li_kind;
    logic [31:0] li_start, li_len;
    logic        li_done, li_busy;

    fosu_line_iter #(.WIN(WIN)) u_lines (
        .clk(clk), .rst_n(rst_n), .start(start), .file_len(file_len),
        .mem_addr(li_addr), .mem_data(mem_data),
        .rec_ready(li_ready), .rec_valid(li_valid),
        .rec_section(li_section), .rec_kind(li_kind),
        .rec_start(li_start), .rec_len(li_len),
        .busy(li_busy), .done(li_done)
    );

    // ---------------------------------------------------------------------
    // Content cursor and the shared numeric scanner, which always looks at
    // the current cursor.
    // ---------------------------------------------------------------------
    typedef enum logic [2:0] {
        C_IDLE, C_TIME, C_COMMA1, C_BL, C_REST_COMMA, C_REST_VAL, C_EMIT, C_FAIL
    } cstate_t;
    cstate_t cstate;

    logic [31:0] cursor, line_stop;
    logic [2:0]  rest_idx;
    logic        punt;

    logic        content_busy;
    always_comb content_busy = (cstate != C_IDLE);

    always_comb mem_addr = content_busy ? cursor : li_addr;

    logic        ns_neg, ns_int_any, ns_dec_any, ns_has_exp, ns_needs_host;
    logic        ns_int_run_full, ns_has_dot;
    logic [7:0]  ns_nd_int, ns_nd_frac, ns_int_len, ns_dec_len, ns_frac;
    logic [63:0] ns_val19, ns_mant;

    fosu_numscan #(.WIN(WIN)) u_num (
        .bytes_in(mem_data),
        .neg(ns_neg), .nd_int(ns_nd_int), .nd_frac(ns_nd_frac),
        .has_dot(ns_has_dot), .has_exp(ns_has_exp),
        .int_any(ns_int_any), .dec_any(ns_dec_any),
        .val19(ns_val19), .int_len(ns_int_len), .int_run_full(ns_int_run_full),
        .mant(ns_mant), .frac(ns_frac), .needs_host(ns_needs_host),
        .dec_len(ns_dec_len)
    );

    logic [7:0] cur_byte;
    always_comb cur_byte = mem_data[7:0];

    // Scanner outputs the remaining section parsers will consume. Sunk
    // explicitly so lint stays strict instead of being globally relaxed.
    logic _unused;
    always_comb _unused = ns_has_exp | ns_has_dot | (|ns_nd_int) |
                          (|ns_nd_frac) | li_busy;

    // Bytes left in this line: every field parse must stay inside it, the way
    // the C++ bounds every parse_* call with `end`.
    logic [31:0] remain;
    always_comb remain = (line_stop > cursor) ? (line_stop - cursor) : 32'd0;

    // A scan is only trustworthy when it fits inside the line. The C++ passes
    // `end` into parse_double/parse_i64; here the window may reach past the
    // line, so a field whose consume length exceeds the remaining bytes is
    // treated as reaching the line end.
    logic dec_fits, int_fits;
    always_comb begin
        dec_fits = ({24'd0, ns_dec_len} <= remain);
        int_fits = ({24'd0, ns_int_len} <= remain);
    end

    // clamp_i32 over a signed magnitude, matching the C++ helper.
    function automatic [31:0] clamp_i32(input logic sgn, input logic [63:0] mag);
        if (!sgn) begin
            if (mag > 64'd2147483647) clamp_i32 = 32'h7FFFFFFF;
            else                      clamp_i32 = mag[31:0];
        end else begin
            if (mag > 64'd2147483648) clamp_i32 = 32'h80000000;
            else                      clamp_i32 = -mag[31:0];
        end
    endfunction

    // Defaults from the C++: TimingPoint{0,0,4,0,0,100,true,0} and
    // rest[6] = {4,0,0,100,1,0}.
    logic [31:0] rest0, rest1, rest2, rest3, rest4, rest5;

    always_comb begin
        rec_valid   = (cstate == C_EMIT) || (cstate == C_FAIL);
        rec_tag     = (cstate == C_EMIT) ? (punt ? TAG_PUNT : TAG_TIMING)
                                        : TAG_MALFORMED;
        rec_start   = li_start;
        rec_len     = li_len;
        rec_section = li_section;

        tp_meter        = rest0;
        tp_sample_set   = rest1;
        tp_sample_index = rest2;
        tp_volume       = rest3;
        tp_uninherited  = (rest4 != 32'd0);
        tp_effects      = rest5;

        // Release the line once content parsing has finished with it, or
        // immediately for sections not yet handled here.
        li_ready = 1'b0;
        if (li_valid && !content_busy) begin
            li_ready = !(li_kind == 2'd2 && li_section == SEC_TIMING);
        end else if ((cstate == C_EMIT || cstate == C_FAIL) && rec_ready) begin
            li_ready = 1'b1;
        end
    end

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            cstate <= C_IDLE;
            done   <= 1'b0;
        end else begin
            done <= li_done && !content_busy;

            case (cstate)
                C_IDLE: begin
                    if (li_valid && li_kind == 2'd2 &&
                        li_section == SEC_TIMING) begin
                        cursor       <= li_start;
                        line_stop    <= li_start + li_len;
                        rest0        <= 32'd4;
                        rest1        <= 32'd0;
                        rest2        <= 32'd0;
                        rest3        <= 32'd100;
                        rest4        <= 32'd1;
                        rest5        <= 32'd0;
                        tp_time_mant <= 64'd0;
                        tp_time_frac <= 8'd0;
                        tp_time_neg  <= 1'b0;
                        tp_bl_mant   <= 64'd0;
                        tp_bl_frac   <= 8'd0;
                        tp_bl_neg    <= 1'b0;
                        punt         <= 1'b0;
                        rest_idx     <= 3'd0;
                        cstate       <= C_TIME;
                    end
                end

                // parse_double for `time`; failing it is a malformed line.
                C_TIME: begin
                    if (!ns_dec_any || !dec_fits) begin
                        cstate <= C_FAIL;
                    end else if (ns_needs_host) begin
                        punt   <= 1'b1;
                        cstate <= C_EMIT;
                    end else begin
                        tp_time_mant <= ns_mant;
                        tp_time_frac <= ns_frac;
                        tp_time_neg  <= ns_neg;
                        cursor       <= cursor + {24'd0, ns_dec_len};
                        cstate       <= C_COMMA1;
                    end
                end

                C_COMMA1: begin
                    if (remain == 0 || cur_byte != CH_COMMA) cstate <= C_FAIL;
                    else begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_BL;
                    end
                end

                C_BL: begin
                    if (!ns_dec_any || !dec_fits) begin
                        cstate <= C_FAIL;
                    end else if (ns_needs_host) begin
                        punt   <= 1'b1;
                        cstate <= C_EMIT;
                    end else begin
                        tp_bl_mant <= ns_mant;
                        tp_bl_frac <= ns_frac;
                        tp_bl_neg  <= ns_neg;
                        cursor     <= cursor + {24'd0, ns_dec_len};
                        cstate     <= C_REST_COMMA;
                    end
                end

                // Up to six optional integer fields, each introduced by a
                // comma. The C++ breaks out of its loop at the first field that
                // isn't there, leaving the rest at their defaults -- so both
                // "no comma" and "comma but no digits" end the line here.
                C_REST_COMMA: begin
                    if (rest_idx == 3'd6 || remain == 0 ||
                        cur_byte != CH_COMMA) begin
                        cstate <= C_EMIT;
                    end else begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_REST_VAL;
                    end
                end

                C_REST_VAL: begin
                    if (!ns_int_any || !int_fits) begin
                        cstate <= C_EMIT;          // parse_i64 consumed nothing
                    end else if (ns_int_run_full) begin
                        punt   <= 1'b1;            // digits outrun the window
                        cstate <= C_EMIT;
                    end else begin
                        case (rest_idx)
                            3'd0: rest0 <= clamp_i32(ns_neg, ns_val19);
                            3'd1: rest1 <= clamp_i32(ns_neg, ns_val19);
                            3'd2: rest2 <= clamp_i32(ns_neg, ns_val19);
                            3'd3: rest3 <= clamp_i32(ns_neg, ns_val19);
                            3'd4: rest4 <= clamp_i32(ns_neg, ns_val19);
                            // effects is a plain uint32 truncation, not clamped.
                            default: rest5 <= ns_neg ? (~ns_val19[31:0] + 32'd1)
                                                     : ns_val19[31:0];
                        endcase
                        cursor   <= cursor + {24'd0, ns_int_len};
                        rest_idx <= rest_idx + 3'd1;
                        cstate   <= C_REST_COMMA;
                    end
                end

                C_EMIT, C_FAIL: begin
                    if (rec_ready) cstate <= C_IDLE;
                end

                default: cstate <= C_IDLE;
            endcase
        end
    end

endmodule

`default_nettype wire

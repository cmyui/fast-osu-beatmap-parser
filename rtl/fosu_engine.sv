// The parser engine: line iteration composed with per-section content parsing.
//
// fosu_line_iter discovers lines and sections and offers them one at a time
// over a valid/ready handshake; this module takes as many cycles as a line
// needs, then releases it. The two share the single memory port -- while a line
// is being parsed the iterator is stalled, so its address outputs are stable
// and the engine steals the port for its own cursor.
//
// Currently implemented: [TimingPoints] and [HitObjects]. The key/value
// sections, [Events] and [Colours] are accepted and released untouched, which
// keeps line iteration verified end to end while they land.
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

    // TAG_HITOBJ payload.
    output logic [31:0] ho_x,
    output logic [31:0] ho_y,
    output logic [31:0] ho_time,
    output logic [31:0] ho_type,
    output logic [31:0] ho_hitsound,
    output logic [31:0] ho_end_time,
    output logic        ho_has_slider,
    output logic [31:0] ho_sample_start,
    output logic [31:0] ho_sample_len,

    // TAG_SLIDER payload. `length` is emitted as parse_double's inputs, like
    // the timing fields.
    output logic [7:0]  sl_curve_type,
    output logic [31:0] sl_slides,
    output logic [63:0] sl_len_mant,
    output logic [7:0]  sl_len_frac,
    output logic        sl_len_neg,
    output logic [31:0] sl_es_start,
    output logic [31:0] sl_es_len,
    output logic [31:0] sl_esets_start,
    output logic [31:0] sl_esets_len,

    // TAG_POINT payload: one slider control point.
    output logic [31:0] pt_x,
    output logic [31:0] pt_y,

    // TAG_KV payload: one key/value field.
    output logic [5:0]  kv_field,
    output logic [2:0]  kv_vtype,
    output logic [63:0] kv_i64,
    output logic        kv_i64_neg,
    output logic [63:0] kv_mant,
    output logic [7:0]  kv_frac,
    output logic        kv_neg,
    output logic [31:0] kv_str_start,
    output logic [31:0] kv_str_len,
    output logic        kv_bool,

    // TAG_PUNT / TAG_MALFORMED payload: the line span.
    output logic [31:0] rec_start,
    output logic [31:0] rec_len,
    output logic [3:0]  rec_section,

    output logic        done
);
    localparam logic [3:0] TAG_TIMING    = 4'd1;
    localparam logic [3:0] TAG_MALFORMED = 4'd2;
    localparam logic [3:0] TAG_PUNT      = 4'd3;
    localparam logic [3:0] TAG_HITOBJ    = 4'd4;
    localparam logic [3:0] TAG_SLIDER    = 4'd5;
    localparam logic [3:0] TAG_POINT     = 4'd6;
    localparam logic [3:0] TAG_KV        = 4'd7;

    localparam logic [3:0] SEC_GENERAL  = 4'd1;
    localparam logic [3:0] SEC_EDITOR   = 4'd2;
    localparam logic [3:0] SEC_METADATA = 4'd3;
    localparam logic [3:0] SEC_DIFF     = 4'd4;
    localparam logic [3:0] SEC_TIMING   = 4'd6;
    localparam logic [3:0] SEC_HITOBJ   = 4'd8;

    localparam logic [7:0] CH_SPACE = 8'h20;

    localparam logic [7:0] CH_COMMA = 8'h2C;
    localparam logic [7:0] CH_COLON = 8'h3A;
    localparam logic [7:0] CH_PIPE  = 8'h7C;

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
    typedef enum logic [4:0] {
        C_IDLE,
        // [TimingPoints]
        C_TIME, C_COMMA1, C_BL, C_REST_COMMA, C_REST_VAL,
        // [HitObjects]: prefix, then one of three tails
        C_HO_PREFIX,
        C_SL_COMMA, C_SL_CURVE, C_SL_PIPE, C_SL_X, C_SL_COLON, C_SL_Y,
        C_SL_PT_EMIT, C_SL_SLIDES_COMMA, C_SL_SLIDES, C_SL_LEN_COMMA,
        C_SL_LEN, C_SL_EXTRA, C_SL_SCAN, C_SL_EMIT,
        C_SP_COMMA, C_SP_END, C_SP_SAMPLE,
        C_CI_SAMPLE,
        C_HO_EMIT,
        // key/value sections
        C_KV_KEY, C_KV_VAL, C_KV_EMIT,
        C_EMIT, C_FAIL
    } cstate_t;
    cstate_t cstate;

    logic [31:0] cursor, line_stop;
    logic [2:0]  rest_idx;
    logic        punt;

    logic        content_busy;
    always_comb content_busy = (cstate != C_IDLE);

    // The extras scan walks its own pointer across windows, so it owns the
    // read port while it runs -- otherwise comma positions would be reported
    // relative to `cursor` while being interpreted relative to `ex_scan`.
    always_comb begin
        if (!content_busy)             mem_addr = li_addr;
        else if (cstate == C_SL_SCAN)  mem_addr = ex_scan;
        else                           mem_addr = cursor;
    end

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

    // The hitobject prefix core, looking at the window at the cursor. Its
    // domain is identical to the C++ fast path (verified separately on 270k
    // candidates); a line it rejects is punted to the host rather than
    // re-implementing the lenient scalar fallback in silicon.
    logic        px_ok;
    logic [31:0] px_x, px_y, px_time, px_type, px_hs;
    logic [7:0]  px_next;
    logic [31:0] px_nlmask_unused;

    fosu_prefix #(.WINDOW(32), .BYTES(WIN)) u_prefix (
        .bytes_in(mem_data),
        .ok(px_ok), .x(px_x), .y(px_y), .time_ms(px_time),
        .obj_type(px_type), .hitsound(px_hs), .next_off(px_next),
        .newline_mask(px_nlmask_unused)
    );

    // Hitobject working state.
    logic [31:0] ho_x_r, ho_y_r, ho_time_r, ho_type_r, ho_hs_r, ho_end_r;
    logic [31:0] ho_smp_start_r, ho_smp_len_r;
    logic        ho_is_slider_r;
    logic [7:0]  sl_curve_r;
    logic [31:0] sl_slides_r;
    logic [63:0] sl_len_mant_r;
    logic [7:0]  sl_len_frac_r;
    logic        sl_len_neg_r;
    logic [31:0] sl_es_s_r, sl_es_l_r, sl_ez_s_r, sl_ez_l_r;
    logic [31:0] pt_x_r, pt_y_r;

    // Extra-field comma scan (edgeSounds / edgeSets / hitSample). A monster
    // slider's extras can be thousands of bytes, so this walks windows the same
    // way the line iterator hunts newlines.
    logic [31:0] ex_scan, ex_base, ex_c0;
    logic        ex_have_c0;

    logic [7:0] cur_byte;
    always_comb cur_byte = mem_data[7:0];

    // Key lookup for [General]/[Editor]/[Metadata]/[Difficulty].
    logic        kv_match;
    logic [5:0]  kv_field_w;
    logic [7:0]  kv_key_len;
    logic [2:0]  kv_vtype_w;

    fosu_kv_key u_kv (
        .section(li_section),
        .k4(mem_data[31:0]),
        .b5(mem_data[47:40]), .b6(mem_data[55:48]),
        .b7(mem_data[63:56]), .b9(mem_data[79:72]),
        .match(kv_match), .field_id(kv_field_w),
        .key_len(kv_key_len), .vtype(kv_vtype_w)
    );

    // kv working registers.
    logic [5:0]  kv_field_r;
    logic [2:0]  kv_vtype_r;
    logic [63:0] kv_i64_r, kv_mant_r;
    logic [7:0]  kv_frac_r;
    logic        kv_neg_r, kv_i64_neg_r, kv_bool_r, kv_emit_r;
    logic [31:0] kv_str_s_r, kv_str_l_r;

    // Value offset: key_len + 1, plus the single space General/Editor emit.
    logic [31:0] kv_off, kv_val_len;
    logic [7:0]  kv_byte_at_off;
    always_comb begin
        kv_byte_at_off = mem_data[8*({24'd0, kv_key_len} + 32'd1) +: 8];
        kv_off = {24'd0, kv_key_len} + 32'd1;
        if (kv_off < li_len && kv_byte_at_off == CH_SPACE) kv_off = kv_off + 32'd1;
        kv_val_len = (li_len > kv_off) ? (li_len - kv_off) : 32'd0;
    end

    // Comma search across the window, for the extras scan.
    logic [WIN-1:0] comma_hit;
    logic           cm_found;
    logic [$clog2(WIN)-1:0] cm_pos;
    always_comb begin
        integer i;
        for (i = 0; i < WIN; i = i + 1)
            comma_hit[i] = (mem_data[8*i +: 8] == CH_COMMA);
    end
    fosu_ffs #(.WIDTH(WIN)) u_findcomma (
        .mask(comma_hit), .pos(cm_pos), .found(cm_found));

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

    // A line is "finished" in one of the terminal states. C_SL_PT_EMIT and
    // C_SL_EMIT also transfer records, but mid-line, so they do not release it.
    logic term_state;
    always_comb term_state = (cstate == C_EMIT) || (cstate == C_FAIL) ||
                             (cstate == C_HO_EMIT) || (cstate == C_KV_EMIT);

    always_comb begin
        // A key/value line with no recognised key, or a value the C++ would
        // leave at its default, emits nothing but still finishes the line.
        rec_valid = (term_state && !(cstate == C_KV_EMIT && !kv_emit_r)) ||
                    (cstate == C_SL_PT_EMIT) || (cstate == C_SL_EMIT);
        if (cstate == C_FAIL)             rec_tag = TAG_MALFORMED;
        else if (cstate == C_KV_EMIT)     rec_tag = punt ? TAG_PUNT : TAG_KV;
        else if (cstate == C_SL_PT_EMIT)  rec_tag = TAG_POINT;
        else if (cstate == C_SL_EMIT)     rec_tag = TAG_SLIDER;
        else if (cstate == C_HO_EMIT)     rec_tag = punt ? TAG_PUNT : TAG_HITOBJ;
        else                              rec_tag = punt ? TAG_PUNT : TAG_TIMING;
        rec_start   = li_start;
        rec_len     = li_len;
        rec_section = li_section;

        tp_meter        = rest0;
        tp_sample_set   = rest1;
        tp_sample_index = rest2;
        tp_volume       = rest3;
        tp_uninherited  = (rest4 != 32'd0);
        tp_effects      = rest5;

        ho_x            = ho_x_r;
        ho_y            = ho_y_r;
        ho_time         = ho_time_r;
        ho_type         = ho_type_r;
        ho_hitsound     = ho_hs_r;
        ho_end_time     = ho_end_r;
        ho_has_slider   = ho_is_slider_r;
        ho_sample_start = ho_smp_start_r;
        ho_sample_len   = ho_smp_len_r;

        sl_curve_type   = sl_curve_r;
        sl_slides       = sl_slides_r;
        sl_len_mant     = sl_len_mant_r;
        sl_len_frac     = sl_len_frac_r;
        sl_len_neg      = sl_len_neg_r;
        sl_es_start     = sl_es_s_r;
        sl_es_len       = sl_es_l_r;
        sl_esets_start  = sl_ez_s_r;
        sl_esets_len    = sl_ez_l_r;

        pt_x            = pt_x_r;
        pt_y            = pt_y_r;

        kv_field        = kv_field_r;
        kv_vtype        = kv_vtype_r;
        kv_i64          = kv_i64_r;
        kv_i64_neg      = kv_i64_neg_r;
        kv_mant         = kv_mant_r;
        kv_frac         = kv_frac_r;
        kv_neg          = kv_neg_r;
        kv_str_start    = kv_str_s_r;
        kv_str_len      = kv_str_l_r;
        kv_bool         = kv_bool_r;

        // Release the line once content parsing has finished with it, or
        // immediately for sections not yet handled here.
        li_ready = 1'b0;
        if (li_valid && !content_busy) begin
            li_ready = !(li_kind == 2'd2 &&
                         (li_section == SEC_TIMING || li_section == SEC_HITOBJ ||
                          ((li_section == SEC_GENERAL ||
                            li_section == SEC_EDITOR ||
                            li_section == SEC_METADATA ||
                            li_section == SEC_DIFF) && li_len >= 32'd5)));
        end else if (term_state && rec_ready) begin
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
                        li_section == SEC_HITOBJ) begin
                        cursor         <= li_start;
                        line_stop      <= li_start + li_len;
                        punt           <= 1'b0;
                        ho_end_r       <= 32'd0;
                        ho_is_slider_r <= 1'b0;
                        ho_smp_start_r <= 32'd0;
                        ho_smp_len_r   <= 32'd0;
                        sl_es_s_r      <= 32'd0;
                        sl_es_l_r      <= 32'd0;
                        sl_ez_s_r      <= 32'd0;
                        sl_ez_l_r      <= 32'd0;
                        cstate         <= C_HO_PREFIX;
                    end else if (li_valid && li_kind == 2'd2 &&
                        (li_section == SEC_GENERAL ||
                         li_section == SEC_EDITOR ||
                         li_section == SEC_METADATA ||
                         li_section == SEC_DIFF) && li_len >= 32'd5) begin
                        // The C++ requires len >= 5 before looking at a key.
                        cursor    <= li_start;
                        line_stop <= li_start + li_len;
                        punt      <= 1'b0;
                        kv_emit_r <= 1'b0;
                        cstate    <= C_KV_KEY;
                    end else if (li_valid && li_kind == 2'd2 &&
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


                // ---------------- [HitObjects] ----------------
                // The prefix core's accepted domain equals the C++ fast path's.
                // A rejection would fall to the lenient scalar parser in the
                // C++ (negatives, 4+ digit coords, decimal coords); rather than
                // duplicate that leniency in silicon, the line goes to the host.
                C_HO_PREFIX: begin
                    if (!px_ok || ({24'd0, px_next} > li_len)) begin
                        punt   <= 1'b1;
                        cstate <= C_HO_EMIT;
                    end else begin
                        ho_x_r    <= px_x;
                        ho_y_r    <= px_y;
                        ho_time_r <= px_time;
                        ho_type_r <= px_type;
                        ho_hs_r   <= px_hs;
                        cursor    <= cursor + {24'd0, px_next};
                        // type bit 1 = slider, bit 3 = spinner, bit 7 = hold.
                        if (px_type[1])                    cstate <= C_SL_COMMA;
                        else if (px_type[3] || px_type[7]) cstate <= C_SP_COMMA;
                        else                               cstate <= C_CI_SAMPLE;
                    end
                end

                // --- slider: curveType|x:y|...,slides,length[,extras] ---
                C_SL_COMMA: begin
                    if (remain == 0 || cur_byte != CH_COMMA) cstate <= C_FAIL;
                    else begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_SL_CURVE;
                    end
                end

                C_SL_CURVE: begin
                    // parse_slider_params rejects an empty parameter list.
                    if (remain == 0) cstate <= C_FAIL;
                    else begin
                        sl_curve_r     <= cur_byte;
                        ho_is_slider_r <= 1'b1;
                        cursor         <= cursor + 32'd1;
                        cstate         <= C_SL_PIPE;
                    end
                end

                C_SL_PIPE: begin
                    if (remain != 0 && cur_byte == CH_PIPE) begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_SL_X;
                    end else begin
                        cstate <= C_SL_SLIDES_COMMA;
                    end
                end

                // parse_coord: equivalent to parse_i64 with clamp_i32.
                C_SL_X: begin
                    if (!ns_int_any || !int_fits || ns_int_run_full) begin
                        cstate <= C_FAIL;
                    end else begin
                        pt_x_r <= clamp_i32(ns_neg, ns_val19);
                        cursor <= cursor + {24'd0, ns_int_len};
                        cstate <= C_SL_COLON;
                    end
                end

                C_SL_COLON: begin
                    if (remain == 0 || cur_byte != CH_COLON) cstate <= C_FAIL;
                    else begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_SL_Y;
                    end
                end

                C_SL_Y: begin
                    if (!ns_int_any || !int_fits || ns_int_run_full) begin
                        cstate <= C_FAIL;
                    end else begin
                        pt_y_r <= clamp_i32(ns_neg, ns_val19);
                        cursor <= cursor + {24'd0, ns_int_len};
                        cstate <= C_SL_PT_EMIT;
                    end
                end

                C_SL_PT_EMIT: begin
                    if (rec_ready) cstate <= C_SL_PIPE;
                end

                C_SL_SLIDES_COMMA: begin
                    if (remain == 0 || cur_byte != CH_COMMA) cstate <= C_FAIL;
                    else begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_SL_SLIDES;
                    end
                end

                // slides is a bare integer of 1..7 digits: the C++ takes
                // digit_run8 (capped at 8) and rejects when run-1 > 6, so an
                // 8-or-more digit run fails. No sign is accepted here.
                C_SL_SLIDES: begin
                    if (ns_nd_int == 8'd0 || ns_nd_int > 8'd7 || ns_neg ||
                        ({24'd0, ns_nd_int} > remain)) begin
                        cstate <= C_FAIL;
                    end else begin
                        sl_slides_r <= ns_val19[31:0];
                        cursor      <= cursor + {24'd0, ns_nd_int};
                        cstate      <= C_SL_LEN_COMMA;
                    end
                end

                C_SL_LEN_COMMA: begin
                    if (remain == 0 || cur_byte != CH_COMMA) cstate <= C_FAIL;
                    else begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_SL_LEN;
                    end
                end

                C_SL_LEN: begin
                    if (!ns_dec_any || !dec_fits) begin
                        cstate <= C_FAIL;
                    end else if (ns_needs_host) begin
                        punt   <= 1'b1;
                        cstate <= C_HO_EMIT;
                    end else begin
                        sl_len_mant_r <= ns_mant;
                        sl_len_frac_r <= ns_frac;
                        sl_len_neg_r  <= ns_neg;
                        cursor        <= cursor + {24'd0, ns_dec_len};
                        cstate        <= C_SL_EXTRA;
                    end
                end

                // The three optional extras are positional, split by up to two
                // commas over whatever remains of the line.
                C_SL_EXTRA: begin
                    if (remain == 0 || cur_byte != CH_COMMA) begin
                        cstate <= C_SL_EMIT;
                    end else begin
                        ex_base    <= cursor + 32'd1;
                        ex_scan    <= cursor + 32'd1;
                        ex_have_c0 <= 1'b0;
                        cursor     <= cursor + 32'd1;
                        cstate     <= C_SL_SCAN;
                    end
                end

                C_SL_SCAN: begin
                    if (cm_found && (ex_scan + {26'd0, cm_pos}) < line_stop) begin
                        if (!ex_have_c0) begin
                            ex_c0      <= ex_scan + {26'd0, cm_pos};
                            ex_have_c0 <= 1'b1;
                            ex_scan    <= ex_scan + {26'd0, cm_pos} + 32'd1;
                        end else begin
                            // Both commas found: extras are fully determined.
                            sl_es_s_r      <= ex_base;
                            sl_es_l_r      <= ex_c0 - ex_base;
                            sl_ez_s_r      <= ex_c0 + 32'd1;
                            sl_ez_l_r      <= (ex_scan + {26'd0, cm_pos}) -
                                              (ex_c0 + 32'd1);
                            ho_smp_start_r <= ex_scan + {26'd0, cm_pos} + 32'd1;
                            ho_smp_len_r   <= line_stop -
                                              (ex_scan + {26'd0, cm_pos} + 32'd1);
                            cstate         <= C_SL_EMIT;
                        end
                    end else if ((ex_scan + WIN) >= line_stop) begin
                        // Ran out of line: fewer than two commas present.
                        if (!ex_have_c0) begin
                            sl_es_s_r <= ex_base;
                            sl_es_l_r <= line_stop - ex_base;
                        end else begin
                            sl_es_s_r <= ex_base;
                            sl_es_l_r <= ex_c0 - ex_base;
                            sl_ez_s_r <= ex_c0 + 32'd1;
                            sl_ez_l_r <= line_stop - (ex_c0 + 32'd1);
                        end
                        cstate <= C_SL_EMIT;
                    end else begin
                        ex_scan <= ex_scan + WIN;
                    end
                end

                C_SL_EMIT: begin
                    if (rec_ready) cstate <= C_HO_EMIT;
                end

                // --- spinner / mania hold: ,endTime[,:]hitSample ---
                C_SP_COMMA: begin
                    if (remain == 0 || cur_byte != CH_COMMA) cstate <= C_FAIL;
                    else begin
                        cursor <= cursor + 32'd1;
                        cstate <= C_SP_END;
                    end
                end

                C_SP_END: begin
                    if (!ns_int_any || !int_fits || ns_int_run_full) begin
                        cstate <= C_FAIL;
                    end else begin
                        ho_end_r <= clamp_i32(ns_neg, ns_val19);
                        cursor   <= cursor + {24'd0, ns_int_len};
                        cstate   <= C_SP_SAMPLE;
                    end
                end

                // A hold separates its sample with ':' and a spinner with ','.
                // Anything else leaves hit_sample empty.
                C_SP_SAMPLE: begin
                    if (ho_type_r[7] && remain != 0 && cur_byte == CH_COLON) begin
                        ho_smp_start_r <= cursor + 32'd1;
                        ho_smp_len_r   <= line_stop - (cursor + 32'd1);
                    end else if (remain != 0 && cur_byte == CH_COMMA) begin
                        ho_smp_start_r <= cursor + 32'd1;
                        ho_smp_len_r   <= line_stop - (cursor + 32'd1);
                    end else begin
                        ho_smp_start_r <= 32'd0;
                        ho_smp_len_r   <= 32'd0;
                    end
                    cstate <= C_HO_EMIT;
                end

                // --- circle: optional trailing hitSample ---
                C_CI_SAMPLE: begin
                    if (remain != 0 && cur_byte == CH_COMMA) begin
                        ho_smp_start_r <= cursor + 32'd1;
                        ho_smp_len_r   <= line_stop - (cursor + 32'd1);
                    end
                    cstate <= C_HO_EMIT;
                end

                C_HO_EMIT: begin
                    if (rec_ready) cstate <= C_IDLE;
                end


                // ---------------- key/value sections ----------------
                // The window still sits on the line start here, so the key
                // matcher and the value offset are both available.
                C_KV_KEY: begin
                    if (!kv_match) begin
                        cstate <= C_KV_EMIT;          // unknown key: no record
                    end else begin
                        kv_field_r <= kv_field_w;
                        kv_vtype_r <= kv_vtype_w;
                        kv_str_s_r <= li_start + kv_off;
                        kv_str_l_r <= kv_val_len;
                        cursor     <= li_start + kv_off;
                        line_stop  <= li_start + li_len;
                        cstate     <= C_KV_VAL;
                    end
                end

                C_KV_VAL: begin
                    kv_emit_r <= 1'b1;
                    case (kv_vtype_r)
                        3'd0: begin                    // string: the raw span
                            cstate <= C_KV_EMIT;
                        end
                        3'd3: begin                    // bool: v[0] == '1'
                            kv_bool_r <= (kv_str_l_r != 32'd0) &&
                                         (cur_byte == 8'h31);
                            cstate    <= C_KV_EMIT;
                        end
                        3'd1, 3'd4: begin              // int32 / int64
                            // parse_i32_field/parse_i64 keep the default when
                            // nothing parses, so emit nothing in that case.
                            if (!ns_int_any || !int_fits || ns_int_run_full) begin
                                kv_emit_r <= 1'b0;
                                if (ns_int_run_full) punt <= 1'b1;
                            end else begin
                                kv_i64_r     <= ns_val19;
                                kv_i64_neg_r <= ns_neg;
                            end
                            cstate <= C_KV_EMIT;
                        end
                        default: begin                 // double
                            if (!ns_dec_any || !dec_fits) begin
                                kv_emit_r <= 1'b0;
                            end else if (ns_needs_host) begin
                                punt <= 1'b1;
                            end else begin
                                kv_mant_r <= ns_mant;
                                kv_frac_r <= ns_frac;
                                kv_neg_r  <= ns_neg;
                            end
                            cstate <= C_KV_EMIT;
                        end
                    endcase
                end

                C_KV_EMIT: begin
                    if (rec_ready || !kv_emit_r) cstate <= C_IDLE;
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

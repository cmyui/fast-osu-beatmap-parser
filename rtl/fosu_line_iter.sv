// Line iteration and section dispatch over a memory-resident .osu file.
//
// ARCHITECTURE NOTE. The C++ parser is handed a fully-resident buffer and walks
// pointers through it; this module does the same thing with a cursor and a
// memory read port, which is what a DMA-fed accelerator looks like. That choice
// is deliberate rather than a shortcut: the corpus census says the longest line
// in 10,011 ranked maps is 285,012 bytes (an Aspire-era `HPDrainRate:` with
// tens of thousands of digits) and the longest hitobject line is 177,447 bytes
// with 22,176 control points. No fixed-width line register can hold that, so
// buffering whole lines is off the table and a walking cursor is the only
// correct structure.
//
// The memory port is a combinational read of WIN bytes at any byte address,
// zero-padded past `file_len` -- the same padding guarantee io.hpp gives the
// C++ parser. In silicon that is a BRAM plus an alignment network, whose
// latency is not pipelined in here yet: this module is about getting behaviour
// 1:1 with the C++ first. Registering the read (and the barrel shifter that
// feeds it) is a throughput exercise, not a behavioural one.
//
// Matches the C++ line loop exactly:
//   - line ends at '\n' or end of file
//   - a trailing '\r' is stripped, but only when the line is non-empty
//   - zero-length lines are skipped
//   - a line starting with '[' sets the section (needs len >= 3)
//   - "//" is a comment
//   - anything else is content, tagged with the current section

`default_nettype none

module fosu_line_iter #(
    parameter int WIN = 64
) (
    input  wire         clk,
    input  wire         rst_n,

    input  wire         start,
    input  wire [31:0]  file_len,

    // Combinational window read: WIN bytes at mem_addr, zero past file_len.
    //
    // A parent MAY steal this port while a record is being held -- but only
    // because the record's contents are LATCHED first (state S_CLASS below).
    // They used to be combinational functions of this window, which meant a
    // parent repointing the port silently rewrote them: a metadata line whose
    // value begins with '[' ("Title:[Yuru Fuwa Jukai Girl]") got reclassified
    // as a section header mid-parse and corrupted the section register.
    output logic [31:0] mem_addr,
    input  wire [8*WIN-1:0] mem_data,

    // One record per non-empty line. Held until rec_ready, so a downstream
    // content parser can take as many cycles as a line needs -- the same
    // valid/ready contract every block here speaks.
    input  wire         rec_ready,
    output logic        rec_valid,
    output logic [3:0]  rec_section,  // matches C++ enum class Section
    output logic [1:0]  rec_kind,     // 0 = header, 1 = comment, 2 = content
    output logic [31:0] rec_start,
    output logic [31:0] rec_len,

    output logic        busy,
    output logic        done
);
    localparam int PW = $clog2(WIN);

    // C++ `enum class Section` values, so the testbench can compare directly.
    localparam logic [3:0] SEC_NONE     = 4'd0;
    localparam logic [3:0] SEC_GENERAL  = 4'd1;
    localparam logic [3:0] SEC_EDITOR   = 4'd2;
    localparam logic [3:0] SEC_METADATA = 4'd3;
    localparam logic [3:0] SEC_DIFF     = 4'd4;
    localparam logic [3:0] SEC_EVENTS   = 4'd5;
    localparam logic [3:0] SEC_TIMING   = 4'd6;
    localparam logic [3:0] SEC_COLOURS  = 4'd7;
    localparam logic [3:0] SEC_HITOBJ   = 4'd8;
    localparam logic [3:0] SEC_UNKNOWN  = 4'd9;

    localparam logic [7:0] CH_NL     = 8'h0A;
    localparam logic [7:0] CH_CR     = 8'h0D;
    localparam logic [7:0] CH_SLASH  = 8'h2F;
    localparam logic [7:0] CH_LBRACK = 8'h5B;

    typedef enum logic [2:0] {
        S_IDLE, S_SCAN, S_CLASS, S_EMIT, S_DONE
    } state_t;
    state_t state;

    // Latched classification: valid from S_EMIT onward, independent of whatever
    // the shared window happens to show.
    logic [3:0]  sec_r;
    logic [1:0]  kind_r;
    logic [31:0] len_r;
    logic        emit_r;

    logic [31:0] line_begin;   // first byte of the current line
    logic [31:0] scan_ptr;     // window position while hunting the newline
    logic [31:0] line_end;     // exclusive end of line body (CR stripped)
    logic [31:0] next_begin;   // where the following line starts
    logic [7:0]  prev_last;    // last byte of the previous window, for a
                               // newline landing at window offset 0
    logic [3:0]  section;

    // ---------------------------------------------------------------------
    // Window views.
    // ---------------------------------------------------------------------
    logic [7:0] win [0:WIN-1];
    always_comb begin
        integer i;
        for (i = 0; i < WIN; i = i + 1) win[i] = mem_data[8*i +: 8];
    end

    // Newline search across the window.
    logic [WIN-1:0]  nl_hit;
    logic            nl_found;
    logic [PW-1:0]   nl_pos;
    always_comb begin
        integer i;
        for (i = 0; i < WIN; i = i + 1) nl_hit[i] = (win[i] == CH_NL);
    end

    fosu_ffs #(.WIDTH(WIN)) u_findnl (
        .mask  (nl_hit),
        .pos   (nl_pos),
        .found (nl_found)
    );

    always_comb begin
        // S_CLASS is the one cycle where the window must sit on the line start.
        mem_addr = (state == S_CLASS) ? line_begin : scan_ptr;
    end

    // ---------------------------------------------------------------------
    // Line-end candidates, computed combinationally so the FSM stays a plain
    // state update (and so Yosys sees no procedural locals).
    //
    // A newline at window offset 0 has its preceding byte in the PREVIOUS
    // window, which is what prev_last carries. The CR strip only applies to a
    // non-empty body, matching the C++ `line_end > p` guard.
    // ---------------------------------------------------------------------
    logic [31:0] nl_abs, eof_rem;
    logic [7:0]  prev_ch, eof_prev_ch;
    logic [31:0] end_at_nl, end_at_eof;
    logic        nl_in_file, at_last_window;

    always_comb begin
        nl_abs     = scan_ptr + {{(32-PW){1'b0}}, nl_pos};
        nl_in_file = nl_found && (nl_abs < file_len);
        prev_ch    = (nl_pos == 0) ? prev_last : win[nl_pos - 1];
        end_at_nl  = (nl_abs > line_begin && prev_ch == CH_CR) ? nl_abs - 32'd1
                                                              : nl_abs;

        at_last_window = (scan_ptr + WIN) >= file_len;
        eof_rem        = file_len - scan_ptr;         // bytes left in this window
        eof_prev_ch    = (eof_rem == 0) ? prev_last : win[eof_rem - 1];
        end_at_eof     = (file_len > line_begin && eof_prev_ch == CH_CR)
                             ? file_len - 32'd1 : file_len;
    end

    // ---------------------------------------------------------------------
    // Line classification, evaluated in S_EMIT where the window sits on
    // line_begin.
    // ---------------------------------------------------------------------
    logic [31:0] body_len;
    logic [3:0]  hdr_section;
    logic [1:0]  kind;
    logic        emit_this;

    always_comb begin
        body_len = line_end - line_begin;

        // Section from the second byte, with the third splitting
        // [Editor]/[Events] -- the C++ trick, unchanged.
        case (win[1])
            8'h47:   hdr_section = SEC_GENERAL;                        // 'G'
            8'h45:   hdr_section = (win[2] == 8'h64) ? SEC_EDITOR      // 'E','d'
                                                    : SEC_EVENTS;
            8'h4D:   hdr_section = SEC_METADATA;                       // 'M'
            8'h44:   hdr_section = SEC_DIFF;                           // 'D'
            8'h54:   hdr_section = SEC_TIMING;                         // 'T'
            8'h43:   hdr_section = SEC_COLOURS;                        // 'C'
            8'h48:   hdr_section = SEC_HITOBJ;                         // 'H'
            default: hdr_section = SEC_UNKNOWN;
        endcase

        if (body_len == 0) begin
            kind      = 2'd0;
            emit_this = 1'b0;                 // blank lines produce nothing
        end else if (win[0] == CH_LBRACK) begin
            kind      = 2'd0;                 // header
            emit_this = 1'b1;
        end else if (body_len >= 2 && win[0] == CH_SLASH &&
                     win[1] == CH_SLASH) begin
            kind      = 2'd1;                 // comment
            emit_this = 1'b1;
        end else begin
            kind      = 2'd2;                 // content
            emit_this = 1'b1;
        end
    end

    // Record outputs come from the latched classification, so they are stable
    // for as long as the consumer needs -- including while it borrows the
    // memory port. Transfer still happens on valid && ready.
    always_comb begin
        rec_valid   = (state == S_EMIT) && emit_r;
        rec_start   = line_begin;
        rec_len     = len_r;
        rec_kind    = kind_r;
        rec_section = sec_r;
    end

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            state     <= S_IDLE;
            done      <= 1'b0;
            busy      <= 1'b0;
            section   <= SEC_NONE;
        end else begin
            case (state)
                S_IDLE: begin
                    done <= 1'b0;
                    if (start) begin
                        section    <= SEC_NONE;
                        line_begin <= 32'd0;
                        scan_ptr   <= 32'd0;
                        prev_last  <= 8'h00;
                        busy       <= 1'b1;
                        state      <= (file_len == 0) ? S_DONE : S_SCAN;
                    end
                end

                S_SCAN: begin
                    if (nl_in_file) begin
                        line_end   <= end_at_nl;
                        next_begin <= nl_abs + 32'd1;
                        state      <= S_CLASS;
                    end else if (at_last_window) begin
                        // No newline before EOF: the last line ends at EOF.
                        line_end   <= end_at_eof;
                        next_begin <= file_len;
                        state      <= S_CLASS;
                    end else begin
                        prev_last <= win[WIN-1];
                        scan_ptr  <= scan_ptr + WIN;
                    end
                end

                // One cycle owning the port to characterise the line.
                S_CLASS: begin
                    kind_r  <= kind;
                    len_r   <= body_len;
                    emit_r  <= emit_this;
                    sec_r   <= (kind == 2'd0) ? hdr_section : section;
                    // The section register updates here too, so it is settled
                    // before the consumer can perturb the window.
                    if (kind == 2'd0 && body_len >= 3) section <= hdr_section;
                    state   <= S_EMIT;
                end

                S_EMIT: begin
                    // Advance only once the record has actually transferred.
                    // rec_valid is combinational, so a consumer that holds
                    // rec_ready high still sees every record -- registering
                    // valid here would skip records for such a consumer, which
                    // is the classic handshake bug.
                    if (!emit_r || rec_ready) begin
                        if (next_begin >= file_len) begin
                            state <= S_DONE;
                        end else begin
                            line_begin <= next_begin;
                            scan_ptr   <= next_begin;
                            prev_last  <= 8'h00;
                            state      <= S_SCAN;
                        end
                    end
                end

                S_DONE: begin
                    busy  <= 1'b0;
                    done  <= 1'b1;
                    state <= S_IDLE;
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule

`default_nettype wire

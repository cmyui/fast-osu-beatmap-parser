// Key lookup for the four key/value sections.
//
// The C++ dispatches these lines on their first four bytes loaded as one u32 --
// editor-emitted keys are unique on that prefix within a section, plus one
// disambiguating byte where two keys collide -- and each key's length then
// locates the value with no memchr and no trim. That is already a table lookup,
// so it becomes a case statement here, which is a mux.
//
// Outputs the field identity, the key length the C++ hardcodes, and how the
// value should be read. Purely combinational.

`default_nettype none

module fosu_kv_key (
    input  wire [3:0]  section,   // C++ enum class Section
    input  wire [31:0] k4,        // first four bytes, little-endian
    input  wire [7:0]  b5,        // p[5]  -- Audio*/Title* disambiguator
    input  wire [7:0]  b6,        // p[6]  -- Sample*/Artist*/Slider*
    input  wire [7:0]  b7,        // p[7]  -- Beatmap(Set)ID
    input  wire [7:0]  b9,        // p[9]  -- Countdown(Offset)

    output logic       match,
    output logic [5:0] field_id,
    output logic [7:0] key_len,
    output logic [2:0] vtype
);
    // Value kinds.
    localparam logic [2:0] V_STR = 3'd0;
    localparam logic [2:0] V_I32 = 3'd1;
    localparam logic [2:0] V_F64 = 3'd2;
    localparam logic [2:0] V_BOOL= 3'd3;
    localparam logic [2:0] V_I64 = 3'd4;

    localparam logic [3:0] SEC_GENERAL  = 4'd1;
    localparam logic [3:0] SEC_EDITOR   = 4'd2;
    localparam logic [3:0] SEC_METADATA = 4'd3;
    localparam logic [3:0] SEC_DIFF     = 4'd4;

    // key4(a,b,c,d) as the C++ builds it.
    function automatic [31:0] k(input [7:0] a, input [7:0] b,
                                input [7:0] c, input [7:0] d);
        k = {d, c, b, a};
    endfunction

    always_comb begin
        match    = 1'b1;
        field_id = 6'd0;
        key_len  = 8'd0;
        vtype    = V_STR;

        case (section)
        // ------------------------------- [General]
        SEC_GENERAL: begin
            if (k4 == k("A","u","d","i")) begin
                if (b5 == "F")      begin field_id=6'd1;  key_len=8'd13; vtype=V_STR; end
                else if (b5 == "L") begin field_id=6'd2;  key_len=8'd11; vtype=V_I32; end
                else                match = 1'b0;
            end else if (k4 == k("P","r","e","v")) begin
                field_id=6'd3;  key_len=8'd11; vtype=V_I32;
            end else if (k4 == k("C","o","u","n")) begin
                if (b9 == "O") begin field_id=6'd4; key_len=8'd15; vtype=V_I32; end
                else           begin field_id=6'd5; key_len=8'd9;  vtype=V_I32; end
            end else if (k4 == k("S","a","m","p")) begin
                if (b6 == "S") begin field_id=6'd6; key_len=8'd9;  vtype=V_STR; end
                else           begin field_id=6'd7; key_len=8'd24; vtype=V_BOOL; end
            end else if (k4 == k("S","t","a","c")) begin
                field_id=6'd8;  key_len=8'd13; vtype=V_F64;
            end else if (k4 == k("M","o","d","e")) begin
                field_id=6'd9;  key_len=8'd4;  vtype=V_I32;
            end else if (k4 == k("L","e","t","t")) begin
                field_id=6'd10; key_len=8'd17; vtype=V_BOOL;
            end else if (k4 == k("W","i","d","e")) begin
                field_id=6'd11; key_len=8'd20; vtype=V_BOOL;
            end else if (k4 == k("E","p","i","l")) begin
                field_id=6'd12; key_len=8'd15; vtype=V_BOOL;
            end else if (k4 == k("S","p","e","c")) begin
                field_id=6'd13; key_len=8'd12; vtype=V_BOOL;
            end else if (k4 == k("U","s","e","S")) begin
                field_id=6'd14; key_len=8'd14; vtype=V_BOOL;
            end else if (k4 == k("O","v","e","r")) begin
                field_id=6'd15; key_len=8'd15; vtype=V_STR;
            end else if (k4 == k("S","k","i","n")) begin
                field_id=6'd16; key_len=8'd14; vtype=V_STR;
            end else match = 1'b0;
        end

        // ------------------------------- [Editor]
        SEC_EDITOR: begin
            if (k4 == k("B","o","o","k")) begin
                field_id=6'd17; key_len=8'd9;  vtype=V_STR;
            end else if (k4 == k("D","i","s","t")) begin
                field_id=6'd18; key_len=8'd15; vtype=V_F64;
            end else if (k4 == k("B","e","a","t")) begin
                field_id=6'd19; key_len=8'd11; vtype=V_I32;
            end else if (k4 == k("G","r","i","d")) begin
                field_id=6'd20; key_len=8'd8;  vtype=V_I32;
            end else if (k4 == k("T","i","m","e")) begin
                field_id=6'd21; key_len=8'd12; vtype=V_F64;
            end else match = 1'b0;
        end

        // ------------------------------- [Metadata]
        SEC_METADATA: begin
            if (k4 == k("T","i","t","l")) begin
                if (b5 == "U") begin field_id=6'd23; key_len=8'd12; vtype=V_STR; end
                else           begin field_id=6'd22; key_len=8'd5;  vtype=V_STR; end
            end else if (k4 == k("A","r","t","i")) begin
                if (b6 == "U") begin field_id=6'd25; key_len=8'd13; vtype=V_STR; end
                else           begin field_id=6'd24; key_len=8'd6;  vtype=V_STR; end
            end else if (k4 == k("C","r","e","a")) begin
                field_id=6'd26; key_len=8'd7;  vtype=V_STR;
            end else if (k4 == k("V","e","r","s")) begin
                field_id=6'd27; key_len=8'd7;  vtype=V_STR;
            end else if (k4 == k("S","o","u","r")) begin
                field_id=6'd28; key_len=8'd6;  vtype=V_STR;
            end else if (k4 == k("T","a","g","s")) begin
                field_id=6'd29; key_len=8'd4;  vtype=V_STR;
            end else if (k4 == k("B","e","a","t")) begin
                // BeatmapID / BeatmapSetID, and both are int64 -- NOT clamped
                // to int32 like the other integer fields.
                if (b7 == "S") begin field_id=6'd31; key_len=8'd12; vtype=V_I64; end
                else           begin field_id=6'd30; key_len=8'd9;  vtype=V_I64; end
            end else match = 1'b0;
        end

        // ------------------------------- [Difficulty]
        SEC_DIFF: begin
            if (k4 == k("H","P","D","r")) begin
                field_id=6'd32; key_len=8'd11; vtype=V_F64;
            end else if (k4 == k("C","i","r","c")) begin
                field_id=6'd33; key_len=8'd10; vtype=V_F64;
            end else if (k4 == k("O","v","e","r")) begin
                field_id=6'd34; key_len=8'd17; vtype=V_F64;
            end else if (k4 == k("A","p","p","r")) begin
                field_id=6'd35; key_len=8'd12; vtype=V_F64;
            end else if (k4 == k("S","l","i","d")) begin
                if (b6 == "M") begin field_id=6'd36; key_len=8'd16; vtype=V_F64; end
                else           begin field_id=6'd37; key_len=8'd14; vtype=V_F64; end
            end else match = 1'b0;
        end

        default: match = 1'b0;
        endcase
    end

endmodule

`default_nettype wire

// =============================================================================
// dct_engine.sv - one 8x8 DCT + quantizer engine with Avalon-ST in/out.
//
// Arithmetic (bit-exact with HPS/src/dct_ref.c):
//   X = pixel - 128
//   T = (X . C^T + 2^14) >>> 15          pass 1 (rows)
//   D = (C . T   + 2^14) >>> 15          pass 2 (columns)
//   q = sign(D) * ((|D| * R + 2^15) >> 16)   R = reciprocal from the Q table
//
// Operation (single buffer, one block at a time):
//   LOAD  : accept 64 uint8 pixels (DATA_WIDTH/8 per beat), level-shift on entry
//   PASS1 : T = X . C^T   using LANES multiply-accumulate units, 8 cycles per output group
//   PASS2 : D = C . T     same units
//   OUT   : send 64 int16 coefficients (DATA_WIDTH/16 per beat), quantized on the fly
//
// Cycles per block (no backpressure): 64*8/DATA_WIDTH + 2*512/LANES + 64*16/DATA_WIDTH
//   LANES = 8, DATA_WIDTH = 64:  8 + 64 + 64 + 16 = 152
//
// Parameters:
//   LANES      : 1, 2, 4, 8, 16, 32 or 64 (must divide 64)
//   DATA_WIDTH : 16, 32, 64, 128, 256 or 512
//
// Data layout: first pixel / coefficient in the lowest bits of a beat; pixels and
// coefficients in row-major order within the block.
//
// See FPGA/DESIGN.md for diagrams and reasoning.
// =============================================================================
module dct_engine #(
    parameter int LANES      = 8,
    parameter int DATA_WIDTH = 64
) (
    input  logic                  clk,
    input  logic                  rst,            // synchronous, active high

    // Q table write port (reciprocals R, 17-bit). Write before streaming blocks.
    input  logic                  q_we,
    input  logic [5:0]            q_addr,
    input  logic [16:0]           q_data,

    // Avalon-ST sink: pixels
    input  logic                  in_valid,
    output logic                  in_ready,
    input  logic [DATA_WIDTH-1:0] in_data,

    // Avalon-ST source: quantized coefficients
    output logic                  out_valid,
    input  logic                  out_ready,
    output logic [DATA_WIDTH-1:0] out_data,
    output logic                  out_sop,
    output logic                  out_eop,

    output logic                  busy            // a block is in progress
);

    localparam int PIX_PER_BEAT  = DATA_WIDTH / 8;
    localparam int COEF_PER_BEAT = DATA_WIDTH / 16;
    localparam int IN_BEATS      = 64 / PIX_PER_BEAT;
    localparam int OUT_BEATS     = 64 / COEF_PER_BEAT;
    localparam int GROUPS        = 64 / LANES;

    // -------------------------------------------------------------------------
    // DCT matrix ROM: C[u][x] in Q1.15, index = u*8 + x (same table as dct_ref.c)
    // -------------------------------------------------------------------------
    function automatic logic signed [15:0] dct_c(input logic [5:0] idx);
        case (idx)
            6'd0,  6'd1,  6'd2,  6'd3,  6'd4,  6'd5,  6'd6,  6'd7:  dct_c =  16'sd11585;
            6'd8:  dct_c =  16'sd16069;  6'd9:  dct_c =  16'sd13623;
            6'd10: dct_c =  16'sd9102;   6'd11: dct_c =  16'sd3196;
            6'd12: dct_c = -16'sd3196;   6'd13: dct_c = -16'sd9102;
            6'd14: dct_c = -16'sd13623;  6'd15: dct_c = -16'sd16069;
            6'd16: dct_c =  16'sd15137;  6'd17: dct_c =  16'sd6270;
            6'd18: dct_c = -16'sd6270;   6'd19: dct_c = -16'sd15137;
            6'd20: dct_c = -16'sd15137;  6'd21: dct_c = -16'sd6270;
            6'd22: dct_c =  16'sd6270;   6'd23: dct_c =  16'sd15137;
            6'd24: dct_c =  16'sd13623;  6'd25: dct_c = -16'sd3196;
            6'd26: dct_c = -16'sd16069;  6'd27: dct_c = -16'sd9102;
            6'd28: dct_c =  16'sd9102;   6'd29: dct_c =  16'sd16069;
            6'd30: dct_c =  16'sd3196;   6'd31: dct_c = -16'sd13623;
            6'd32: dct_c =  16'sd11585;  6'd33: dct_c = -16'sd11585;
            6'd34: dct_c = -16'sd11585;  6'd35: dct_c =  16'sd11585;
            6'd36: dct_c =  16'sd11585;  6'd37: dct_c = -16'sd11585;
            6'd38: dct_c = -16'sd11585;  6'd39: dct_c =  16'sd11585;
            6'd40: dct_c =  16'sd9102;   6'd41: dct_c = -16'sd16069;
            6'd42: dct_c =  16'sd3196;   6'd43: dct_c =  16'sd13623;
            6'd44: dct_c = -16'sd13623;  6'd45: dct_c = -16'sd3196;
            6'd46: dct_c =  16'sd16069;  6'd47: dct_c = -16'sd9102;
            6'd48: dct_c =  16'sd6270;   6'd49: dct_c = -16'sd15137;
            6'd50: dct_c =  16'sd15137;  6'd51: dct_c = -16'sd6270;
            6'd52: dct_c = -16'sd6270;   6'd53: dct_c =  16'sd15137;
            6'd54: dct_c = -16'sd15137;  6'd55: dct_c =  16'sd6270;
            6'd56: dct_c =  16'sd3196;   6'd57: dct_c = -16'sd9102;
            6'd58: dct_c =  16'sd13623;  6'd59: dct_c = -16'sd16069;
            6'd60: dct_c =  16'sd16069;  6'd61: dct_c = -16'sd13623;
            6'd62: dct_c =  16'sd9102;   6'd63: dct_c = -16'sd3196;
            default: dct_c = 16'sd0;
        endcase
    endfunction

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    typedef enum logic [1:0] { S_LOAD, S_PASS1, S_PASS2, S_OUT } state_t;

    state_t      state;
    logic [6:0]  beat;      // input / output beat counter
    logic [2:0]  k;         // dot-product step 0..7
    logic [6:0]  grp;       // output group 0..GROUPS-1

    // Storage (not reset: always written before being read)
    logic signed [7:0]  x_mem [0:63];   // level-shifted pixels
    logic signed [15:0] t_mem [0:63];   // pass-1 result
    logic signed [15:0] d_mem [0:63];   // pass-2 result (DCT coefficients)
    logic        [16:0] r_mem [0:63];   // reciprocals

    // -------------------------------------------------------------------------
    // Multiply-accumulate lanes
    //   Lane l works on output o = grp*LANES + l, i.e. element (row, col) = (o/8, o%8).
    //   PASS1: T[row][col] += X[row][k] * C[col][k]
    //   PASS2: D[row][col] += C[row][k] * T[k][col]
    //   At k = 7 the rounded result is written to t_mem / d_mem.
    // -------------------------------------------------------------------------
    logic        [5:0]  lane_o   [0:LANES-1];
    logic signed [15:0] lane_res [0:LANES-1];

    for (genvar l = 0; l < LANES; l++) begin : g_lane
        logic        [5:0]  o;
        logic        [2:0]  row, col;
        logic signed [15:0] a, b;
        logic signed [31:0] prod, sum, acc;
        logic signed [7:0]  xv;

        assign o   = 6'((int'(grp) * LANES) + l);
        assign row = o[5:3];
        assign col = o[2:0];
        assign xv  = x_mem[{row, k}];

        always_comb begin
            if (state == S_PASS1) begin
                a = {{8{xv[7]}}, xv};
                b = dct_c({col, k});
            end else begin
                a = dct_c({row, k});
                b = t_mem[{k, col}];
            end
        end

        assign prod = a * b;
        assign sum  = acc + prod;

        always_ff @(posedge clk) begin
            if (rst)
                acc <= '0;
            else if (state == S_PASS1 || state == S_PASS2)
                acc <= (k == 3'd7) ? '0 : sum;
        end

        assign lane_o[l]   = o;
        assign lane_res[l] = 16'((sum + 32'sd16384) >>> 15);   // round, then shift
    end

    // -------------------------------------------------------------------------
    // Control and storage writes
    // -------------------------------------------------------------------------
    always_ff @(posedge clk) begin
        if (q_we)
            r_mem[q_addr] <= q_data;

        if (rst) begin
            state <= S_LOAD;
            beat  <= '0;
            k     <= '0;
            grp   <= '0;
        end else begin
            case (state)
                S_LOAD: begin
                    if (in_valid) begin
                        for (int j = 0; j < PIX_PER_BEAT; j++)
                            // pixel - 128 in two's complement = pixel XOR 0x80
                            x_mem[6'((int'(beat) * PIX_PER_BEAT) + j)] <= in_data[j*8 +: 8] ^ 8'h80;
                        if (beat == 7'(IN_BEATS - 1)) begin
                            beat  <= '0;
                            k     <= '0;
                            grp   <= '0;
                            state <= S_PASS1;
                        end else begin
                            beat <= beat + 7'd1;
                        end
                    end
                end

                S_PASS1, S_PASS2: begin
                    if (k == 3'd7) begin
                        for (int l = 0; l < LANES; l++) begin
                            if (state == S_PASS1)
                                t_mem[lane_o[l]] <= lane_res[l];
                            else
                                d_mem[lane_o[l]] <= lane_res[l];
                        end
                        k <= '0;
                        if (grp == 7'(GROUPS - 1)) begin
                            grp   <= '0;
                            state <= (state == S_PASS1) ? S_PASS2 : S_OUT;
                        end else begin
                            grp <= grp + 7'd1;
                        end
                    end else begin
                        k <= k + 3'd1;
                    end
                end

                S_OUT: begin
                    if (out_ready) begin
                        if (beat == 7'(OUT_BEATS - 1)) begin
                            beat  <= '0;
                            state <= S_LOAD;
                        end else begin
                            beat <= beat + 7'd1;
                        end
                    end
                end

                default: state <= S_LOAD;
            endcase
        end
    end

    // -------------------------------------------------------------------------
    // Output: quantize COEF_PER_BEAT coefficients per beat, combinationally
    //   q = sign(D) * ((|D| * R + 2^15) >> 16)
    // -------------------------------------------------------------------------
    for (genvar j = 0; j < COEF_PER_BEAT; j++) begin : g_quant
        logic        [5:0]  idx;
        logic signed [15:0] dv;
        logic        [15:0] mag;
        logic        [32:0] prod;
        logic        [15:0] qmag;

        assign idx  = 6'((int'(beat) * COEF_PER_BEAT) + j);
        assign dv   = d_mem[idx];
        assign mag  = dv[15] ? 16'(-dv) : 16'(dv);
        assign prod = mag * r_mem[idx];
        assign qmag = 16'((prod + 33'd32768) >> 16);
        assign out_data[j*16 +: 16] = dv[15] ? 16'(-qmag) : qmag;
    end

    assign in_ready  = (state == S_LOAD) && !rst;
    assign out_valid = (state == S_OUT);
    assign out_sop   = (beat == 7'd0);
    assign out_eop   = (beat == 7'(OUT_BEATS - 1));
    assign busy      = (state != S_LOAD) || (beat != 7'd0);

endmodule

`timescale 1ns/1ps

module dct_quantizer (
    input  logic        clk,
    input  logic        rst,
    input  logic        start,

    // 64 pixels, row-major order
    // pixel[0]  = row 0, column 0
    // pixel[1]  = row 0, column 1
    // ...
    // pixel[63] = row 7, column 7
    input  logic [7:0]  pixel [0:63],

    // Quantized DCT coefficients
    output logic signed [15:0] qcoeff [0:63],

    output logic        done
);

    // ============================================================
    // Fixed-point configuration
    // ============================================================
    //
    // DCT constants are represented as:
    //
    //       real_value * 2^14
    //
    // Therefore:
    //
    //       SCALE = 16384
    //
    // Example:
    //
    //       1/sqrt(8) = 0.353553...
    //
    //       0.353553 * 16384 = 5793
    //
    // ============================================================

    localparam integer SCALE = 16384;

    // ============================================================
    // DCT coefficient matrix
    //
    // C[u][x] =
    //
    // alpha(u) * cos((2*x+1)*u*pi/16)
    //
    // scaled by 2^14.
    //
    // ============================================================

    integer C [0:7][0:7];

    initial begin

        C[0][0] = 5793;
        C[0][1] = 5793;
        C[0][2] = 5793;
        C[0][3] = 5793;
        C[0][4] = 5793;
        C[0][5] = 5793;
        C[0][6] = 5793;
        C[0][7] = 5793;

        C[1][0] = 8035;
        C[1][1] = 6811;
        C[1][2] = 4551;
        C[1][3] = 1598;
        C[1][4] = -1598;
        C[1][5] = -4551;
        C[1][6] = -6811;
        C[1][7] = -8035;

        C[2][0] = 7568;
        C[2][1] = 3135;
        C[2][2] = -3135;
        C[2][3] = -7568;
        C[2][4] = -7568;
        C[2][5] = -3135;
        C[2][6] = 3135;
        C[2][7] = 7568;

        C[3][0] = 6811;
        C[3][1] = -1598;
        C[3][2] = -8035;
        C[3][3] = -4551;
        C[3][4] = 4551;
        C[3][5] = 8035;
        C[3][6] = 1598;
        C[3][7] = -6811;

        C[4][0] = 5793;
        C[4][1] = -5793;
        C[4][2] = -5793;
        C[4][3] = 5793;
        C[4][4] = 5793;
        C[4][5] = -5793;
        C[4][6] = -5793;
        C[4][7] = 5793;

        C[5][0] = 4551;
        C[5][1] = -8035;
        C[5][2] = 1598;
        C[5][3] = 6811;
        C[5][4] = -6811;
        C[5][5] = -1598;
        C[5][6] = 8035;
        C[5][7] = -4551;

        C[6][0] = 3135;
        C[6][1] = -7568;
        C[6][2] = 7568;
        C[6][3] = -3135;
        C[6][4] = -3135;
        C[6][5] = 7568;
        C[6][6] = -7568;
        C[6][7] = 3135;

        C[7][0] = 1598;
        C[7][1] = -4551;
        C[7][2] = 6811;
        C[7][3] = -8035;
        C[7][4] = 8035;
        C[7][5] = -6811;
        C[7][6] = 4551;
        C[7][7] = -1598;

    end

    // ============================================================
    // JPEG luminance quantization table
    //
    // Standard JPEG table.
    //
    // ============================================================

    integer Q [0:63];

    initial begin

        Q[0]  = 16;
        Q[1]  = 11;
        Q[2]  = 10;
        Q[3]  = 16;
        Q[4]  = 24;
        Q[5]  = 40;
        Q[6]  = 51;
        Q[7]  = 61;

        Q[8]  = 12;
        Q[9]  = 12;
        Q[10] = 14;
        Q[11] = 19;
        Q[12] = 26;
        Q[13] = 58;
        Q[14] = 60;
        Q[15] = 55;

        Q[16] = 14;
        Q[17] = 13;
        Q[18] = 16;
        Q[19] = 24;
        Q[20] = 40;
        Q[21] = 57;
        Q[22] = 69;
        Q[23] = 56;

        Q[24] = 14;
        Q[25] = 17;
        Q[26] = 22;
        Q[27] = 29;
        Q[28] = 51;
        Q[29] = 87;
        Q[30] = 80;
        Q[31] = 62;

        Q[32] = 18;
        Q[33] = 22;
        Q[34] = 37;
        Q[35] = 56;
        Q[36] = 68;
        Q[37] = 109;
        Q[38] = 103;
        Q[39] = 77;

        Q[40] = 24;
        Q[41] = 35;
        Q[42] = 55;
        Q[43] = 64;
        Q[44] = 81;
        Q[45] = 104;
        Q[46] = 113;
        Q[47] = 92;

        Q[48] = 49;
        Q[49] = 64;
        Q[50] = 78;
        Q[51] = 87;
        Q[52] = 103;
        Q[53] = 121;
        Q[54] = 120;
        Q[55] = 101;

        Q[56] = 72;
        Q[57] = 92;
        Q[58] = 95;
        Q[59] = 98;
        Q[60] = 112;
        Q[61] = 100;
        Q[62] = 103;
        Q[63] = 99;

    end

    // ============================================================
    // Intermediate matrices
    // ============================================================

    integer X    [0:7][0:7];
    integer TEMP [0:7][0:7];
    integer DCT  [0:7][0:7];

    integer r;
    integer c;
    integer k;

    integer sum;

    integer temp_value;
    integer dct_value;

    // ============================================================
    // Main DCT + quantization process
    //
    // This is a reference implementation.
    //
    // It performs:
    //
    //       X = pixel - 128
    //
    //       TEMP = X * C^T
    //
    //       DCT = C * TEMP
    //
    // ============================================================

    always_ff @(posedge clk) begin

        if (rst) begin

            done <= 1'b0;

            for (r = 0; r < 8; r = r + 1) begin
                for (c = 0; c < 8; c = c + 1) begin

                    X[r][c]    <= 0;
                    TEMP[r][c] <= 0;
                    DCT[r][c]  <= 0;

                end
            end

            for (k = 0; k < 64; k = k + 1) begin
                qcoeff[k] <= 16'sd0;
            end

        end

        else begin

            done <= 1'b0;

            if (start) begin

                // ====================================================
                // STEP 1: Level shift
                //
                // JPEG DCT normally operates on:
                //
                //       pixel - 128
                //
                // ====================================================

                for (r = 0; r < 8; r = r + 1) begin

                    for (c = 0; c < 8; c = c + 1) begin

                        X[r][c] = $signed({1'b0, pixel[r*8+c]}) - 128;

                    end

                end

                // ====================================================
                // STEP 2: Horizontal 1-D DCT
                //
                // TEMP[r][u] =
                //
                //       sum X[r][x] * C[u][x]
                //
                // divided by SCALE.
                //
                // ====================================================

                for (r = 0; r < 8; r = r + 1) begin

                    for (c = 0; c < 8; c = c + 1) begin

                        sum = 0;

                        for (k = 0; k < 8; k = k + 1) begin

                            sum = sum + X[r][k] * C[c][k];

                        end

                        // Fixed-point scaling.
                        //
                        // + SCALE/2 gives nearest-integer rounding
                        // for positive values.
                        //
                        // For the reference design we use arithmetic
                        // division, which is easy to understand.

                        if (sum >= 0)
                            TEMP[r][c] = (sum + SCALE/2) / SCALE;
                        else
                            TEMP[r][c] = -((-sum + SCALE/2) / SCALE);

                    end

                end

                // ====================================================
                // STEP 3: Vertical 1-D DCT
                //
                // DCT[u][v] =
                //
                //       sum C[u][y] * TEMP[y][v]
                //
                // divided by SCALE.
                //
                // ====================================================

                for (r = 0; r < 8; r = r + 1) begin

                    for (c = 0; c < 8; c = c + 1) begin

                        sum = 0;

                        for (k = 0; k < 8; k = k + 1) begin

                            sum = sum + C[r][k] * TEMP[k][c];

                        end

                        if (sum >= 0)
                            DCT[r][c] = (sum + SCALE/2) / SCALE;
                        else
                            DCT[r][c] = -((-sum + SCALE/2) / SCALE);

                    end

                end

                // ====================================================
                // STEP 4: Quantization
                //
                //       QDCT = round(DCT / Q)
                //
                // ====================================================

                for (r = 0; r < 8; r = r + 1) begin

                    for (c = 0; c < 8; c = c + 1) begin

                        k = r*8+c;

                        if (DCT[r][c] >= 0) begin

                            qcoeff[k] <=
                                (DCT[r][c] + Q[k]/2) / Q[k];

                        end

                        else begin

                            qcoeff[k] <=
                                -((-DCT[r][c] + Q[k]/2) / Q[k]);

                        end

                    end

                end

                done <= 1'b1;

            end

        end

    end

endmodule

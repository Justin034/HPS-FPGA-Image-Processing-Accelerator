module matrix_mult_8x8 (
    input  logic clk,
    input  logic rst,

    // =================================================
    // Control
    // =================================================

    input  logic start,
    output logic busy,
    output logic done,

    // =================================================
    // Matrix A write interface
    //
    // A = integer values
    // =================================================

    input  logic        a_we,
    input  logic [5:0]  a_addr,
    input  logic signed [15:0] a_data,

    // =================================================
    // Matrix B write interface
    //
    // B = Q1.15 values
    // =================================================

    input  logic        b_we,
    input  logic [5:0]  b_addr,
    input  logic signed [15:0] b_data,

    // =================================================
    // Matrix C read interface
    //
    // C = integer result
    // =================================================

    input  logic [5:0] c_addr,
    output logic signed [15:0] c_data
);


    // =================================================
    // Matrix storage
    // =================================================

    logic signed [15:0] A [0:63];
    logic signed [15:0] B [0:63];
    logic signed [15:0] C [0:63];


    // =================================================
    // Counters
    // =================================================

    logic [2:0] row;
    logic [2:0] col;
    logic [2:0] k;


    // =================================================
    // Arithmetic
    //
    // A = integer
    // B = Q1.15
    //
    // integer × Q1.15 = Q?.15
    //
    // After >>> 15:
    // result = integer
    // =================================================

    logic signed [15:0] a_value;
    logic signed [15:0] b_value;

    logic signed [31:0] product;

    logic signed [34:0] accumulator;


    // =================================================
    // Current multiplication
    // =================================================

    always_comb begin

        a_value = A[row * 8 + k];

        b_value = B[k * 8 + col];

        product = a_value * b_value;

    end


    // =================================================
    // Read C
    // =================================================

    always_comb begin

        c_data = C[c_addr];

    end


    // =================================================
    // Main controller
    // =================================================

    always_ff @(posedge clk or posedge rst) begin

        if (rst) begin

            row         <= 3'd0;
            col         <= 3'd0;
            k           <= 3'd0;

            accumulator <= 35'sd0;

            busy <= 1'b0;
            done <= 1'b0;

        end

        else begin

            // done is a one-clock pulse
            done <= 1'b0;


            // =================================================
            // Write A and B
            // =================================================

            if (!busy) begin

                if (a_we)
                    A[a_addr] <= a_data;

                if (b_we)
                    B[b_addr] <= b_data;

            end


            // =================================================
            // Start
            // =================================================

            if (start && !busy) begin

                row         <= 3'd0;
                col         <= 3'd0;
                k           <= 3'd0;

                accumulator <= 35'sd0;

                busy <= 1'b1;

            end


            // =================================================
            // Matrix multiplication
            // =================================================

            else if (busy) begin


                // ------------------------------------------------
                // Last multiplication for this C element
                // ------------------------------------------------

                if (k == 3'd7) begin

                    // Add final product
                    //
                    // Product has 15 fractional bits.
                    // Shift right by 15 to get integer result.

                    C[row * 8 + col] <=
                        (accumulator + product) >>> 15;


                    // Clear accumulator
                    accumulator <= 35'sd0;


                    // Reset k
                    k <= 3'd0;


                    // ------------------------------------------------
                    // Next column
                    // ------------------------------------------------

                    if (col == 3'd7) begin

                        col <= 3'd0;


                        // ------------------------------------------------
                        // Next row
                        // ------------------------------------------------

                        if (row == 3'd7) begin

                            row <= 3'd0;

                            busy <= 1'b0;
                            done <= 1'b1;

                        end

                        else begin

                            row <= row + 3'd1;

                        end

                    end

                    else begin

                        col <= col + 3'd1;

                    end

                end


                // ------------------------------------------------
                // Continue accumulating
                // ------------------------------------------------

                else begin

                    accumulator <= accumulator + product;

                    k <= k + 3'd1;

                end

            end

        end

    end

endmodule

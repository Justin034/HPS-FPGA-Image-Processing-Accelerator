module matrix_mult_8x8 (
    input  logic clk,
    input  logic rst,

    // ------------------------------------------------
    // Control
    // ------------------------------------------------

    input  logic start,
    output logic busy,
    output logic done,

    // ------------------------------------------------
    // Matrix A write interface
    // ------------------------------------------------

    input  logic        a_we,
    input  logic [5:0]  a_addr,
    input  logic signed [15:0] a_data,

    // ------------------------------------------------
    // Matrix B write interface
    // ------------------------------------------------

    input  logic        b_we,
    input  logic [5:0]  b_addr,
    input  logic signed [15:0] b_data,

    // ------------------------------------------------
    // Matrix C read interface
    // ------------------------------------------------

    input  logic [5:0]  c_addr,
    output logic signed [15:0] c_data
);


    // =================================================
    // Matrix storage
    //
    // All values are signed Q1.15
    //
    // 64 elements × 16 bits
    // =================================================

    logic signed [15:0] A [0:63];
    logic signed [15:0] B [0:63];
    logic signed [15:0] C [0:63];


    // =================================================
    // Matrix multiplication counters
    // =================================================

    logic [2:0] row;
    logic [2:0] col;
    logic [2:0] k;


    // =================================================
    // Arithmetic
    // =================================================

    logic signed [15:0] a_value;
    logic signed [15:0] b_value;

    logic signed [31:0] product;

    // 35 bits gives enough room for
    // accumulating 8 Q2.30 products.
    logic signed [34:0] accumulator;


    // =================================================
    // Address calculation
    //
    // A[row][k] = A[row*8 + k]
    // B[k][col] = B[k*8 + col]
    // C[row][col] = C[row*8 + col]
    // =================================================

    always_comb begin

        a_value = A[row * 8 + k];

        b_value = B[k * 8 + col];

        product = a_value * b_value;

    end


    // =================================================
    // Read result matrix
    // =================================================

    always_comb begin

        c_data = C[c_addr];

    end


    // =================================================
    // Main controller
    // =================================================

    always_ff @(posedge clk or posedge rst) begin

        if (rst) begin

            row <= 3'd0;
            col <= 3'd0;
            k <= 3'd0;

            accumulator <= 35'sd0;

            busy <= 1'b0;
            done <= 1'b0;

        end

        else begin

            // -----------------------------------------
            // Default
            // -----------------------------------------

            done <= 1'b0;


            // -----------------------------------------
            // Write matrix A
            // -----------------------------------------

            if (a_we && !busy) begin

                A[a_addr] <= a_data;

            end


            // -----------------------------------------
            // Write matrix B
            // -----------------------------------------

            if (b_we && !busy) begin

                B[b_addr] <= b_data;

            end


            // -----------------------------------------
            // Start multiplication
            // -----------------------------------------

            if (start && !busy) begin

                row <= 3'd0;
                col <= 3'd0;
                k <= 3'd0;

                accumulator <= 35'sd0;

                busy <= 1'b1;

            end


            // -----------------------------------------
            // Matrix multiplication
            // -----------------------------------------

            else if (busy) begin

                // -------------------------------------
                // Last k value
                //
                // We are calculating:
                //
                // C[row][col] =
                // sum(A[row][k] * B[k][col])
                // -------------------------------------

                if (k == 3'd7) begin

                    // Add final product.
                    //
                    // Product is Q2.30.
                    // Shift right 15 to return
                    // to Q1.15.

                    C[row * 8 + col] <=
                        (accumulator + product) >>> 15;


                    accumulator <= 35'sd0;

                    k <= 3'd0;


                    // ---------------------------------
                    // Move to next column
                    // ---------------------------------

                    if (col == 3'd7) begin

                        col <= 3'd0;


                        // -----------------------------
                        // Move to next row
                        // -----------------------------

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

                // -------------------------------------
                // Continue accumulating
                // -------------------------------------

                else begin

                    accumulator <= accumulator + product;

                    k <= k + 3'd1;

                end

            end

        end

    end

endmodule
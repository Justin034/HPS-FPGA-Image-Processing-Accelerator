`timescale 1ns/1ps

module Matrix_Mult_tb;

    localparam int W = 8;

    logic [W-1:0] A [0:7][0:7];
    logic [W-1:0] B [0:7][0:7];

    logic [2*W+2:0] C [0:7][0:7];

    logic [2*W+2:0] expected [0:7][0:7];

    integer i, j, k;

    // Device Under Test
    matrix_mult_8x8 #(
        .W(W)
    ) dut (
        .A(A),
        .B(B),
        .C(C)
    );

    initial begin

        // Initialize matrices
        for (i = 0; i < 8; i++) begin
            for (j = 0; j < 8; j++) begin
                A[i][j] = i + j;
                B[i][j] = (i == j) ? 1 : 0;
            end
        end

        // Calculate expected result
        for (i = 0; i < 8; i++) begin
            for (j = 0; j < 8; j++) begin

                expected[i][j] = 0;

                for (k = 0; k < 8; k++) begin
                    expected[i][j] =
                        expected[i][j] +
                        A[i][k] * B[k][j];
                end

            end
        end

        // Give combinational logic time to settle
        #1;

        // Check results
        for (i = 0; i < 8; i++) begin
            for (j = 0; j < 8; j++) begin

                if (C[i][j] !== expected[i][j]) begin
                    $error(
                        "Mismatch C[%0d][%0d]: expected %0d, got %0d",
                        i, j, expected[i][j], C[i][j]
                    );
                end

            end
        end

        $display("Matrix multiplication test PASSED!");

        // Print result
        $display("Result matrix:");

        for (i = 0; i < 8; i++) begin
            for (j = 0; j < 8; j++) begin
                $write("%4d ", C[i][j]);
            end
            $display("");
        end

        $finish;
    end

endmodule

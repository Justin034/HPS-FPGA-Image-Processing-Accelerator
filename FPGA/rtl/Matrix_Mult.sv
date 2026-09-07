module matrix_mult_8x8 #(
    parameter int W = 8
) (
    input  logic [W-1:0] A [0:7][0:7],
    input  logic [W-1:0] B [0:7][0:7],
    output logic [2*W+2:0] C [0:7][0:7]
);

    integer i, j, k;

    always_comb begin

        // Calculate every element of C
        for (i = 0; i < 8; i++) begin
            for (j = 0; j < 8; j++) begin

                C[i][j] = 0;

                for (k = 0; k < 8; k++) begin
                    C[i][j] = C[i][j] + A[i][k] * B[k][j];
                end

            end
        end

    end

endmodule

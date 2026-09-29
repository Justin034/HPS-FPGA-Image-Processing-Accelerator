`timescale 1ns/1ps

module matrix_mult_8x8_tb;

    // =================================================
    // Clock and reset
    // =================================================

    logic clk;
    logic rst;


    // =================================================
    // Control
    // =================================================

    logic start;
    logic busy;
    logic done;


    // =================================================
    // Matrix A write interface
    // =================================================

    logic        a_we;
    logic [5:0]  a_addr;
    logic signed [15:0] a_data;


    // =================================================
    // Matrix B write interface
    // =================================================

    logic        b_we;
    logic [5:0]  b_addr;
    logic signed [15:0] b_data;


    // =================================================
    // Matrix C read interface
    // =================================================

    logic [5:0]  c_addr;
    logic signed [15:0] c_data;


    // =================================================
    // Instantiate DUT
    // =================================================

    matrix_mult_8x8 DUT (
        .clk    (clk),
        .rst    (rst),

        .start  (start),
        .busy   (busy),
        .done   (done),

        .a_we   (a_we),
        .a_addr (a_addr),
        .a_data (a_data),

        .b_we   (b_we),
        .b_addr (b_addr),
        .b_data (b_data),

        .c_addr (c_addr),
        .c_data (c_data)
    );


    // =================================================
    // Clock
    // 10 ns period
    // =================================================

    initial begin
        clk = 1'b0;

        forever #5 clk = ~clk;
    end


    // =================================================
    // Write one value to A
    // =================================================

    task write_A(
        input [5:0] addr,
        input logic signed [15:0] data
    );

        begin

            @(negedge clk);

            a_addr = addr;
            a_data = data;
            a_we   = 1'b1;

            @(negedge clk);

            a_we = 1'b0;

        end

    endtask


    // =================================================
    // Write one value to B
    // =================================================

    task write_B(
        input [5:0] addr,
        input logic signed [15:0] data
    );

        begin

            @(negedge clk);

            b_addr = addr;
            b_data = data;
            b_we   = 1'b1;

            @(negedge clk);

            b_we = 1'b0;

        end

    endtask


    // =================================================
    // Test
    // =================================================

    integer i;
    integer j;

    integer errors;

    initial begin

        // ------------------------------------------------
        // Initial values
        // ------------------------------------------------

        rst   = 1'b1;
        start = 1'b0;

        a_we   = 1'b0;
        a_addr = 6'd0;
        a_data = 16'sd0;

        b_we   = 1'b0;
        b_addr = 6'd0;
        b_data = 16'sd0;

        c_addr = 6'd0;

        errors = 0;


        // ------------------------------------------------
        // Reset
        // ------------------------------------------------

        #20;

        rst = 1'b0;

        #10;


        // =================================================
        // Load Matrix A
        //
        // A = 0.5 * Identity
        //
        // Q1.15:
        //
        // 0.5 = 16384
        // =================================================

        $display("");
        $display("Loading Matrix A...");

        for (i = 0; i < 8; i = i + 1) begin

            for (j = 0; j < 8; j = j + 1) begin

                if (i == j)
                    write_A(i * 8 + j, 16'sd16384);

                else
                    write_A(i * 8 + j, 16'sd0);

            end

        end


        // =================================================
        // Load Matrix B
        //
        // B = 0.25 * Identity
        //
        // Q1.15:
        //
        // 0.25 = 8192
        // =================================================

        $display("Loading Matrix B...");

        for (i = 0; i < 8; i = i + 1) begin

            for (j = 0; j < 8; j = j + 1) begin

                if (i == j)
                    write_B(i * 8 + j, 16'sd8192);

                else
                    write_B(i * 8 + j, 16'sd0);

            end

        end


        // =================================================
        // Start multiplication
        // =================================================

        $display("");
        $display("Starting matrix multiplication...");

        @(negedge clk);

        start = 1'b1;

        @(negedge clk);

        start = 1'b0;


        // =================================================
        // Wait for calculation to finish
        // =================================================

        wait (done == 1'b1);

        $display("Matrix multiplication complete.");
        $display("");


        // =================================================
        // Read Matrix C
        //
        // Expected:
        //
        // C = 0.125 * Identity
        //
        // Q1.15:
        //
        // 0.125 = 4096
        // =================================================

        $display("========================================");
        $display("RESULT MATRIX C");
        $display("========================================");

        for (i = 0; i < 8; i = i + 1) begin

            for (j = 0; j < 8; j = j + 1) begin

                c_addr = i * 8 + j;

                #1;

                $write("%6d ", c_data);

            end

            $display("");

        end


        // =================================================
        // Check results
        // =================================================

        $display("");
        $display("========================================");
        $display("CHECKING RESULTS");
        $display("========================================");


        for (i = 0; i < 8; i = i + 1) begin

            for (j = 0; j < 8; j = j + 1) begin

                c_addr = i * 8 + j;

                #1;

                if (i == j) begin

                    if (c_data !== 16'sd4096) begin

                        $display(
                            "ERROR: C[%0d][%0d] = %0d, expected 4096",
                            i,
                            j,
                            c_data
                        );

                        errors = errors + 1;

                    end

                end

                else begin

                    if (c_data !== 16'sd0) begin

                        $display(
                            "ERROR: C[%0d][%0d] = %0d, expected 0",
                            i,
                            j,
                            c_data
                        );

                        errors = errors + 1;

                    end

                end

            end

        end


        // =================================================
        // Final result
        // =================================================

        $display("");

        if (errors == 0) begin

            $display("========================================");
            $display("TEST PASSED");
            $display("========================================");

        end

        else begin

            $display("========================================");
            $display("TEST FAILED");
            $display("Errors = %0d", errors);
            $display("========================================");

        end


        #20;

        $finish;

    end

endmodule

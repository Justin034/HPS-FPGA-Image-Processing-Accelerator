`timescale 1ns/1ps

module tb_dct_quantizer;

    // ============================================================
    // Clock and reset
    // ============================================================

    logic clk;
    logic rst;
    logic start;

    logic [7:0] pixel [0:63];

    logic signed [15:0] qcoeff [0:63];

    logic done;

    // ============================================================
    // DUT
    // ============================================================

    dct_quantizer DUT (
        .clk    (clk),
        .rst    (rst),
        .start  (start),
        .pixel  (pixel),
        .qcoeff (qcoeff),
        .done   (done)
    );

    // ============================================================
    // Clock
    // ============================================================

    initial begin
        clk = 1'b0;

        forever #5 clk = ~clk;
    end

    // ============================================================
    // Print input block
    // ============================================================

    task print_input;

        integer r;
        integer c;

        begin

            $display("");
            $display("==============================================");
            $display("INPUT BLOCK");
            $display("==============================================");

            for (r = 0; r < 8; r = r + 1) begin

                for (c = 0; c < 8; c = c + 1) begin

                    $write("%4d ", pixel[r*8+c]);

                end

                $display("");

            end

        end

    endtask

    // ============================================================
    // Print quantized coefficients
    // ============================================================

    task print_output;

        integer r;
        integer c;

        begin

            $display("");
            $display("==============================================");
            $display("QUANTIZED DCT");
            $display("==============================================");

            for (r = 0; r < 8; r = r + 1) begin

                for (c = 0; c < 8; c = c + 1) begin

                    $write("%6d ", qcoeff[r*8+c]);

                end

                $display("");

            end

        end

    endtask

    // ============================================================
    // Run one DCT operation
    // ============================================================

    task run_dct;

        begin

            @(negedge clk);

            start = 1'b1;

            @(negedge clk);

            start = 1'b0;

            wait(done == 1'b1);

            @(negedge clk);

        end

    endtask

    // ============================================================
    // Test 1
    //
    // All pixels = 128
    //
    // Level shift:
    //
    //       128 - 128 = 0
    //
    // Therefore all DCT coefficients should be zero.
    // ============================================================

    task test_zero_block;

        integer i;
        integer errors;

        begin

            $display("");
            $display("==============================================");
            $display("TEST 1: ZERO AFTER LEVEL SHIFT");
            $display("==============================================");

            for (i = 0; i < 64; i = i + 1)
                pixel[i] = 8'd128;

            print_input();

            run_dct();

            print_output();

            errors = 0;

            for (i = 0; i < 64; i = i + 1) begin

                if (qcoeff[i] !== 0) begin

                    $display(
                        "ERROR: qcoeff[%0d] = %0d, expected 0",
                        i,
                        qcoeff[i]
                    );

                    errors = errors + 1;

                end

            end

            if (errors == 0)
                $display("TEST 1 PASSED");
            else
                $display("TEST 1 FAILED: %0d errors", errors);

        end

    endtask

    // ============================================================
    // Test 2
    //
    // Constant 255 block.
    //
    // After level shift:
    //
    //       255 - 128 = 127
    //
    // For a constant block, theoretically only DC is non-zero.
    //
    // DC = 8 * 8 * 127 / 8
    //
    //     = 1016
    //
    // Quantization:
    //
    //     1016 / 16 = 63.5
    //
    // Rounded = 64
    //
    // ============================================================

    task test_constant_255;

        integer i;
        integer errors;

        begin

            $display("");
            $display("==============================================");
            $display("TEST 2: CONSTANT 255 BLOCK");
            $display("==============================================");

            for (i = 0; i < 64; i = i + 1)
                pixel[i] = 8'd255;

            print_input();

            run_dct();

            print_output();

            errors = 0;

            if (qcoeff[0] !== 16'sd64) begin

                $display(
                    "ERROR: DC = %0d, expected 64",
                    qcoeff[0]
                );

                errors = errors + 1;

            end

            for (i = 1; i < 64; i = i + 1) begin

                if (qcoeff[i] !== 0) begin

                    $display(
                        "ERROR: qcoeff[%0d] = %0d, expected 0",
                        i,
                        qcoeff[i]
                    );

                    errors = errors + 1;

                end

            end

            if (errors == 0)
                $display("TEST 2 PASSED");
            else
                $display("TEST 2 FAILED: %0d errors", errors);

        end

    endtask

    // ============================================================
    // Test 3
    //
    // Horizontal gradient.
    //
    // ============================================================

    task test_gradient;

        integer r;
        integer c;

        begin

            $display("");
            $display("==============================================");
            $display("TEST 3: GRADIENT");
            $display("==============================================");

            for (r = 0; r < 8; r = r + 1) begin

                for (c = 0; c < 8; c = c + 1) begin

                    pixel[r*8+c] = 8'd64 + c*16;

                end

            end

            print_input();

            run_dct();

            print_output();

            $display("TEST 3 COMPLETED");

        end

    endtask

    // ============================================================
    // Test 4
    //
    // Random image block.
    // ============================================================

    task test_random;

        integer i;

        begin

            $display("");
            $display("==============================================");
            $display("TEST 4: RANDOM BLOCK");
            $display("==============================================");

            for (i = 0; i < 64; i = i + 1)
                pixel[i] = $urandom_range(0,255);

            print_input();

            run_dct();

            print_output();

            $display("TEST 4 COMPLETED");

        end

    endtask

    // ============================================================
    // Main simulation
    // ============================================================

    initial begin

        rst   = 1'b1;
        start = 1'b0;

        for (integer i = 0; i < 64; i = i + 1)
            pixel[i] = 8'd0;

        // Reset
        repeat(3) @(posedge clk);

        rst = 1'b0;

        // Run tests
        test_zero_block();

        test_constant_255();

        test_gradient();

        test_random();

        $display("");
        $display("==============================================");
        $display("ALL TESTS COMPLETED");
        $display("==============================================");

        #50;

        $finish;

    end

endmodule

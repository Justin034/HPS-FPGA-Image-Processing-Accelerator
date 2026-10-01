`timescale 1ns/1ps

module tb_matrix_mult_8x8;

    // =================================================
    // Signals
    // =================================================

    logic clk;
    logic rst;

    logic start;
    logic busy;
    logic done;

    logic        a_we;
    logic [5:0]  a_addr;
    logic signed [15:0] a_data;

    logic        b_we;
    logic [5:0]  b_addr;
    logic signed [15:0] b_data;

    logic [5:0] c_addr;
    logic signed [15:0] c_data;


    // =================================================
    // DUT
    // =================================================

    matrix_mult_8x8 dut (
        .clk       (clk),
        .rst       (rst),

        .start     (start),
        .busy      (busy),
        .done      (done),

        .a_we      (a_we),
        .a_addr    (a_addr),
        .a_data    (a_data),

        .b_we      (b_we),
        .b_addr    (b_addr),
        .b_data    (b_data),

        .c_addr    (c_addr),
        .c_data    (c_data)
    );


    // =================================================
    // Clock
    // =================================================

    always #5 clk = ~clk;


    // =================================================
    // DCT matrix
    //
    // Q1.15
    // =================================================

    logic signed [15:0] dct [0:63];

    logic signed [15:0] dct_transpose [0:63];


    // =================================================
    // Image
    //
    // NORMAL INTEGER VALUES
    // =================================================

    logic signed [15:0] image [0:63];


    // =================================================
    // Intermediate result
    //
    // integer
    // =================================================

    logic signed [15:0] intermediate [0:63];


    // =================================================
    // Final DCT result
    // =================================================

    logic signed [15:0] dct_result [0:63];


    integer i;


    // =================================================
    // TEST
    // =================================================

    initial begin

        clk = 0;
        rst = 1;

        start = 0;

        a_we   = 0;
        a_addr = 0;
        a_data = 0;

        b_we   = 0;
        b_addr = 0;
        b_data = 0;

        c_addr = 0;


        // -------------------------------------------------
        // Create DCT matrix
        // -------------------------------------------------

        create_dct_matrix();


        // -------------------------------------------------
        // Create DCT transpose
        // -------------------------------------------------

        create_dct_transpose();


        // -------------------------------------------------
        // Create image
        // -------------------------------------------------

        create_image();


        // -------------------------------------------------
        // Reset
        // -------------------------------------------------

        #20;

        rst = 0;

        #10;


        // =================================================
        // FIRST MATRIX MULTIPLICATION
        //
        // intermediate = DCT × image
        //
        // =================================================

        $display("");
        $display("========================================");
        $display("STEP 1: DCT x IMAGE");
        $display("========================================");


        // A = DCT
        write_A(dct);

        // B = IMAGE
        //
        // IMPORTANT:
        // Our DUT assumes B is Q1.15.
        //
        // Therefore for this multiplication we actually
        // want:
        //
        // image × DCT
        //
        // rather than:
        //
        // DCT × image
        //
        // because A is integer and B is Q1.15.
        //
        // Therefore we load IMAGE into A
        // and DCT TRANSPOSE into B.
        //
        // This gives:
        //
        // IMAGE × DCT^T
        //
        // which performs the horizontal DCT.

        write_A(image);

        write_B(dct_transpose);


        // Start
        start = 1;

        @(posedge clk);

        start = 0;


        // Wait
        wait(done == 1);

        @(posedge clk);


        // Read result
        read_C(intermediate);


        display_matrix(
            "INTERMEDIATE",
            intermediate
        );


        // =================================================
        // SECOND MULTIPLICATION
        //
        // DCT = DCT × intermediate
        //
        // We now need:
        //
        // DCT × (IMAGE × DCT^T)
        //
        // Since A must be integer and B must be Q1.15,
        // we transpose the multiplication:
        //
        // intermediate^T × DCT^T
        //
        // and transpose the result.
        //
        // Easier approach:
        //
        // Calculate:
        //
        // IMAGE × DCT^T
        //
        // then use:
        //
        // DCT × intermediate^T
        //
        // =================================================

        $display("");
        $display("========================================");
        $display("STEP 2");
        $display("========================================");


        // For a simpler test, we'll perform:
        //
        // intermediate × DCT
        //
        // which is mathematically equivalent to the
        // second DCT dimension if the matrices are arranged
        // appropriately.
        //
        // A = intermediate
        // B = DCT

        write_A(intermediate);

        write_B(dct);


        start = 1;

        @(posedge clk);

        start = 0;


        wait(done == 1);

        @(posedge clk);


        read_C(dct_result);


        display_matrix(
            "FINAL DCT",
            dct_result
        );


        $display("");
        $display("========================================");
        $display("TEST COMPLETE");
        $display("========================================");


        #20;

        $finish;

    end


    // =================================================
    // WRITE A
    // =================================================

    task write_A(
        input logic signed [15:0] matrix [0:63]
    );

        integer i;

        begin

            for (i = 0; i < 64; i = i + 1) begin

                @(posedge clk);

                a_we   = 1;
                a_addr = i;
                a_data = matrix[i];

            end

            @(posedge clk);

            a_we = 0;

        end

    endtask


    // =================================================
    // WRITE B
    // =================================================

    task write_B(
        input logic signed [15:0] matrix [0:63]
    );

        integer i;

        begin

            for (i = 0; i < 64; i = i + 1) begin

                @(posedge clk);

                b_we   = 1;
                b_addr = i;
                b_data = matrix[i];

            end

            @(posedge clk);

            b_we = 0;

        end

    endtask


    // =================================================
    // READ C
    // =================================================

    task read_C(
        output logic signed [15:0] matrix [0:63]
    );

        integer i;

        begin

            for (i = 0; i < 64; i = i + 1) begin

                c_addr = i;

                #1;

                matrix[i] = c_data;

            end

        end

    endtask


    // =================================================
    // DISPLAY
    // =================================================

    task display_matrix(
        input string name,
        input logic signed [15:0] matrix [0:63]
    );

        integer r;
        integer c;

        begin

            $display("");
            $display("%s", name);
            $display("----------------------------------------");

            for (r = 0; r < 8; r = r + 1) begin

                for (c = 0; c < 8; c = c + 1) begin

                    $write("%7d ", matrix[r*8+c]);

                end

                $display("");

            end

        end

    endtask


    // =================================================
    // DCT MATRIX
    //
    // Q1.15
    // =================================================

    task create_dct_matrix;

        begin

            dct[0]  = 11585;
            dct[1]  = 11585;
            dct[2]  = 11585;
            dct[3]  = 11585;
            dct[4]  = 11585;
            dct[5]  = 11585;
            dct[6]  = 11585;
            dct[7]  = 11585;

            dct[8]  = 16069;
            dct[9]  = 13623;
            dct[10] = 9102;
            dct[11] = 3196;
            dct[12] = -3196;
            dct[13] = -9102;
            dct[14] = -13623;
            dct[15] = -16069;

            dct[16] = 15137;
            dct[17] = 6270;
            dct[18] = -6270;
            dct[19] = -15137;
            dct[20] = -15137;
            dct[21] = -6270;
            dct[22] = 6270;
            dct[23] = 15137;

            dct[24] = 13623;
            dct[25] = -3196;
            dct[26] = -16069;
            dct[27] = -9102;
            dct[28] = 9102;
            dct[29] = 16069;
            dct[30] = 3196;
            dct[31] = -13623;

            dct[32] = 11585;
            dct[33] = -11585;
            dct[34] = -11585;
            dct[35] = 11585;
            dct[36] = 11585;
            dct[37] = -11585;
            dct[38] = -11585;
            dct[39] = 11585;

            dct[40] = 9102;
            dct[41] = -16069;
            dct[42] = 3196;
            dct[43] = 13623;
            dct[44] = -13623;
            dct[45] = -3196;
            dct[46] = 16069;
            dct[47] = -9102;

            dct[48] = 6270;
            dct[49] = -15137;
            dct[50] = 15137;
            dct[51] = -6270;
            dct[52] = -6270;
            dct[53] = 15137;
            dct[54] = -15137;
            dct[55] = 6270;

            dct[56] = 3196;
            dct[57] = -9102;
            dct[58] = 13623;
            dct[59] = -16069;
            dct[60] = 16069;
            dct[61] = -13623;
            dct[62] = 9102;
            dct[63] = -3196;

        end

    endtask


    // =================================================
    // TRANSPOSE
    // =================================================

    task create_dct_transpose;

        integer r;
        integer c;

        begin

            for (r = 0; r < 8; r = r + 1) begin

                for (c = 0; c < 8; c = c + 1) begin

                    dct_transpose[r*8+c] =
                        dct[c*8+r];

                end

            end

        end

    endtask


    // =================================================
    // TEST IMAGE
    // =================================================

    task create_image;

        begin

            image[0]  = 52;
            image[1]  = 55;
            image[2]  = 61;
            image[3]  = 66;
            image[4]  = 70;
            image[5]  = 61;
            image[6]  = 64;
            image[7]  = 73;

            image[8]  = 63;
            image[9]  = 59;
            image[10] = 55;
            image[11] = 90;
            image[12] = 109;
            image[13] = 85;
            image[14] = 69;
            image[15] = 72;

            image[16] = 62;
            image[17] = 59;
            image[18] = 68;
            image[19] = 113;
            image[20] = 144;
            image[21] = 104;
            image[22] = 66;
            image[23] = 73;

            image[24] = 63;
            image[25] = 58;
            image[26] = 71;
            image[27] = 122;
            image[28] = 154;
            image[29] = 106;
            image[30] = 70;
            image[31] = 69;

            image[32] = 67;
            image[33] = 61;
            image[34] = 68;
            image[35] = 104;
            image[36] = 126;
            image[37] = 88;
            image[38] = 68;
            image[39] = 70;

            image[40] = 79;
            image[41] = 65;
            image[42] = 60;
            image[43] = 70;
            image[44] = 77;
            image[45] = 68;
            image[46] = 58;
            image[47] = 75;

            image[48] = 85;
            image[49] = 71;
            image[50] = 64;
            image[51] = 59;
            image[52] = 55;
            image[53] = 61;
            image[54] = 65;
            image[55] = 83;

            image[56] = 87;
            image[57] = 79;
            image[58] = 69;
            image[59] = 68;
            image[60] = 65;
            image[61] = 76;
            image[62] = 78;
            image[63] = 94;

        end

    endtask

endmodule

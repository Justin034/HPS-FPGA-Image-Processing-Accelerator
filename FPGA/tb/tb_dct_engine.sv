// =============================================================================
// tb_dct_engine.sv - self-checking Verilator testbench for dct_engine.
//
// Test data and expected results come from the C reference (HPS/src) through
// DPI-C (FPGA/tb/dpi_ref.c). No vector files.
//
// Plusargs:
//   +seed=N        seed number, for the log (pass +verilator+seed+N as well)
//   +nrand=N       random blocks after the 79 edge cases (default 200)
//   +scale=N       Q table scale (default 1)
//   +stall_in=P    % of cycles with in_valid low  (default 20)
//   +stall_out=P   % of cycles with out_ready low (default 30)
//   +logdir=DIR    append the result to DIR/sim.{md,csv} (default: no log)
//
// Drives and samples on the falling clock edge so there are no races with the
// design, which updates on the rising edge.
// =============================================================================
`timescale 1ns/1ps

module tb_dct_engine;

    parameter int LANES      = 8;
    parameter int DATA_WIDTH = 64;

    localparam int PIX_PER_BEAT  = DATA_WIDTH / 8;
    localparam int COEF_PER_BEAT = DATA_WIDTH / 16;
    localparam int IN_BEATS      = 64 / PIX_PER_BEAT;
    localparam int OUT_BEATS     = 64 / COEF_PER_BEAT;

    // ---- C reference (dpi_ref.c) ----
    import "DPI-C" function int    dpi_test_block_count(input int n_random);
    import "DPI-C" function string dpi_test_block_name(input int block);
    import "DPI-C" function int    dpi_test_pixel(input int block, input int i);
    import "DPI-C" function int    dpi_recip(input int scale, input int i);
    import "DPI-C" function int    dpi_ref_q(input int block, input int scale, input int i);
    import "DPI-C" function int    dpi_ref_t(input int block, input int scale, input int i);
    import "DPI-C" function int    dpi_ref_d(input int block, input int scale, input int i);
    import "DPI-C" function void   dpi_log_sim(input string logdir, input string test,
                                               input string cfg, input int seed,
                                               input int blocks, input string cycles_per_block,
                                               input string result, input string details);

    // ---- clock and reset: 50 MHz ----
    logic clk = 1'b0;
    logic rst = 1'b1;
    always #10 clk = ~clk;

    longint cycle = 0;
    always @(posedge clk) cycle <= cycle + 1;

    // ---- DUT ----
    logic                  q_we = 1'b0;
    logic [5:0]            q_addr = '0;
    logic [16:0]           q_data = '0;
    logic                  in_valid = 1'b0;
    logic                  in_ready;
    logic [DATA_WIDTH-1:0] in_data = '0;
    logic                  out_valid;
    logic                  out_ready = 1'b0;
    logic [DATA_WIDTH-1:0] out_data;
    logic                  out_sop, out_eop, busy;

    dct_engine #(.LANES(LANES), .DATA_WIDTH(DATA_WIDTH)) dut (
        .clk, .rst,
        .q_we, .q_addr, .q_data,
        .in_valid, .in_ready, .in_data,
        .out_valid, .out_ready, .out_data, .out_sop, .out_eop,
        .busy
    );

    // ---- settings ----
    int    seed = 1, nrand = 200, scale = 1, stall_in = 20, stall_out = 30;
    string logdir = "";
    int    nblocks;

    // ---- checking state ----
    int     errors = 0;
    int     bad_blocks = 0;
    int     out_block = 0, out_beat = 0;
    bit     block_bad = 0;
    longint start_cycle = 0, end_cycle = 0;

    // Which stage first goes wrong for a block (called while its data is still in the DUT)
    task automatic diagnose(input int b);
        int t_bad = 0, d_bad = 0, first_t = -1, first_d = -1;
        for (int i = 0; i < 64; i++) begin
            if (int'(dut.t_mem[i]) != dpi_ref_t(b, scale, i)) begin
                t_bad++;
                if (first_t < 0) first_t = i;
            end
            if (int'(dut.d_mem[i]) != dpi_ref_d(b, scale, i)) begin
                d_bad++;
                if (first_d < 0) first_d = i;
            end
        end
        if (t_bad != 0)
            $display("    pass 1 (T) wrong in %0d places, first at %0d: got %0d, expected %0d",
                     t_bad, first_t, dut.t_mem[first_t], dpi_ref_t(b, scale, first_t));
        else if (d_bad != 0)
            $display("    pass 1 OK; pass 2 (D) wrong in %0d places, first at %0d: got %0d, expected %0d",
                     d_bad, first_d, dut.d_mem[first_d], dpi_ref_d(b, scale, first_d));
        else
            $display("    pass 1 and pass 2 OK -> quantizer or output path is wrong");
    endtask

    task automatic check_beat();
        for (int j = 0; j < COEF_PER_BEAT; j++) begin
            int idx = out_beat * COEF_PER_BEAT + j;
            logic signed [15:0] got = out_data[j*16 +: 16];
            int exp = dpi_ref_q(out_block, scale, idx);
            if (int'(got) != exp) begin
                errors++;
                if (!block_bad && bad_blocks < 10) begin
                    $display("MISMATCH block %0d (%s), coefficient %0d (%0d,%0d): got %0d, expected %0d",
                             out_block, dpi_test_block_name(out_block), idx, idx / 8, idx % 8,
                             got, exp);
                    diagnose(out_block);
                end
                if (!block_bad) bad_blocks++;
                block_bad = 1;
            end
        end
        if (out_sop != (out_beat == 0) || out_eop != (out_beat == OUT_BEATS - 1)) begin
            errors++;
            if (errors <= 10)
                $display("FRAMING ERROR block %0d beat %0d: sop %0b eop %0b", out_block, out_beat,
                         out_sop, out_eop);
        end
        out_beat++;
        if (out_beat == OUT_BEATS) begin
            out_beat  = 0;
            out_block++;
            block_bad = 0;
            end_cycle = cycle;
        end
    endtask

    // ---- output side: random backpressure, check every accepted beat ----
    initial begin
        forever begin
            @(negedge clk);
            out_ready = rst ? 1'b0 : ($urandom_range(0, 99) >= stall_out);
            if (!rst && out_valid && out_ready)
                check_beat();                 // the beat transfers at the next rising edge
        end
    end

    // ---- input side ----
    task automatic send_beat(input logic [DATA_WIDTH-1:0] data);
        // called just after a falling edge
        while ($urandom_range(0, 99) < stall_in) begin
            in_valid = 1'b0;
            @(negedge clk);
        end
        in_valid = 1'b1;
        in_data  = data;
        while (!in_ready) @(negedge clk);
        @(negedge clk);                       // transfer happened at the rising edge
        in_valid = 1'b0;
    endtask

    initial begin
        logic [DATA_WIDTH-1:0] data;
        string cfg, cpb, result, details;
        real   cycles_per_block;
        longint timeout;

        void'($value$plusargs("seed=%d", seed));
        void'($value$plusargs("nrand=%d", nrand));
        void'($value$plusargs("scale=%d", scale));
        void'($value$plusargs("stall_in=%d", stall_in));
        void'($value$plusargs("stall_out=%d", stall_out));
        void'($value$plusargs("logdir=%s", logdir));
        nblocks = dpi_test_block_count(nrand);
        timeout = longint'(nblocks * (IN_BEATS + OUT_BEATS + 1024 / LANES + 16) * 20 + 1000);

        $display("tb_dct_engine: LANES=%0d DATA_WIDTH=%0d scale=%0d seed=%0d blocks=%0d stall %0d%%/%0d%%",
                 LANES, DATA_WIDTH, scale, seed, nblocks, stall_in, stall_out);

        // reset
        repeat (5) @(negedge clk);
        rst = 1'b0;

        // load the reciprocal table
        for (int i = 0; i < 64; i++) begin
            q_we   = 1'b1;
            q_addr = 6'(i);
            q_data = 17'(dpi_recip(scale, i));
            @(negedge clk);
        end
        q_we = 1'b0;
        @(negedge clk);

        // stream all blocks
        start_cycle = cycle;
        for (int b = 0; b < nblocks; b++) begin
            for (int beat = 0; beat < IN_BEATS; beat++) begin
                for (int j = 0; j < PIX_PER_BEAT; j++)
                    data[j*8 +: 8] = 8'(dpi_test_pixel(b, beat * PIX_PER_BEAT + j));
                send_beat(data);
            end
        end

        // wait for the last results
        while (out_block < nblocks && cycle - start_cycle < timeout) @(negedge clk);

        // report
        cycles_per_block = real'(end_cycle - start_cycle) / real'(nblocks);
        cfg = $sformatf("dct_engine LANES=%0d DATA_WIDTH=%0d scale=%0d stall=%0d/%0d",
                        LANES, DATA_WIDTH, scale, stall_in, stall_out);
        cpb = $sformatf("%0.1f", cycles_per_block);
        if (out_block < nblocks) begin
            result  = "FAIL";
            details = $sformatf("TIMEOUT: %0d of %0d blocks received", out_block, nblocks);
        end else if (errors != 0) begin
            result  = "FAIL";
            details = $sformatf("%0d errors in %0d blocks", errors, bad_blocks);
        end else begin
            result  = "PASS";
            details = $sformatf("all %0d coefficients bit-exact", nblocks * 64);
        end
        $display("%s: %s (%s cycles/block)", result, details, cpb);
        if (logdir != "")
            dpi_log_sim(logdir, "dct_engine", cfg, seed, nblocks, cpb, result, details);
        $finish;
    end

endmodule

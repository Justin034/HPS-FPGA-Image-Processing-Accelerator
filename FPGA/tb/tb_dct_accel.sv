// =============================================================================
// tb_dct_accel.sv - self-checking Verilator testbench for dct_accel.
//
// Drives the two Avalon-MM slaves the way the HPS will (register reads/writes,
// buffer memcpy) and the Avalon-ST ports the way the mSGDMAs will. Test blocks
// and expected coefficients come from the C reference through DPI-C.
//
// Sequence:
//   1. Registers: INFO, MAGIC, BUF_BLOCKS, SCRATCH, idle STATUS, Q_TABLE readback
//   2. ERROR cases: bad MODE, NUM_BLOCKS = 0, NUM_BLOCKS > BUF_BLOCKS
//   3. MODE 0 (option 1): every test block through IN_BLOCK / OUT_BLOCK
//   4. MODE 1 (option 2): all blocks through the buffer in one START
//                         (some words written in two halves to test byteenable)
//   5. START while busy -> ERROR, SOFT_RESET mid-job, then a short re-run
//   6. MODE 2 (options 3/4): all blocks through the stream ports with random
//                         backpressure; checks SOP/EOP and that no extra beats
//                         are accepted before START or after the job
//
// Plusargs: +seed=N +nrand=N +scale=N +stall_in=P +stall_out=P +logdir=DIR
// (stall_* only affect the mode-2 streams)
// =============================================================================
`timescale 1ns/1ps

module tb_dct_accel;

    parameter int NUM_ENGINES   = 1;
    parameter int LANES         = 8;
    parameter int DATA_WIDTH    = 64;
    parameter int BUF_IN_WORDS  = 8192;
    parameter int BUF_OUT_WORDS = 16384;

    localparam int PIX_PER_BEAT  = DATA_WIDTH / 8;
    localparam int COEF_PER_BEAT = DATA_WIDTH / 16;
    localparam int IN_BEATS      = 64 / PIX_PER_BEAT;
    localparam int OUT_BEATS     = 64 / COEF_PER_BEAT;
    localparam int BUF_AW        = $clog2(BUF_IN_WORDS + BUF_OUT_WORDS);
    localparam int BUF_BLOCKS    = (BUF_IN_WORDS / IN_BEATS < BUF_OUT_WORDS / OUT_BEATS)
                                   ? BUF_IN_WORDS / IN_BEATS : BUF_OUT_WORDS / OUT_BEATS;

    // Register byte offsets
    localparam int R_CTRL = 'h000, R_STATUS = 'h004, R_NUM_BLOCKS = 'h008, R_CYCLES = 'h00C;
    localparam int R_INFO = 'h010, R_BUF_BLOCKS = 'h014, R_MAGIC = 'h018, R_SCRATCH = 'h01C;
    localparam int R_QTAB = 'h100, R_IN_BLOCK = 'h200, R_OUT_BLOCK = 'h300;

    localparam logic [31:0] START = 32'h1, SOFT_RESET = 32'h2;
    localparam logic [31:0] ST_BUSY = 32'h1, ST_DONE = 32'h2, ST_ERROR = 32'h4;

    // ---- C reference (dpi_ref.c) ----
    import "DPI-C" function int    dpi_test_block_count(input int n_random);
    import "DPI-C" function string dpi_test_block_name(input int block);
    import "DPI-C" function int    dpi_test_pixel(input int block, input int i);
    import "DPI-C" function int    dpi_recip(input int scale, input int i);
    import "DPI-C" function int    dpi_ref_q(input int block, input int scale, input int i);
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
    logic [7:0]              avs_csr_address = '0;
    logic                    avs_csr_read = 1'b0, avs_csr_write = 1'b0;
    logic [31:0]             avs_csr_writedata = '0;
    logic [31:0]             avs_csr_readdata;
    logic [BUF_AW-1:0]       avs_buf_address = '0;
    logic                    avs_buf_read = 1'b0, avs_buf_write = 1'b0;
    logic [DATA_WIDTH-1:0]   avs_buf_writedata = '0;
    logic [DATA_WIDTH/8-1:0] avs_buf_byteenable = '0;
    logic [DATA_WIDTH-1:0]   avs_buf_readdata;
    logic                    asi_valid = 1'b0;
    logic                    asi_ready;
    logic [DATA_WIDTH-1:0]   asi_data = '0;
    logic                    aso_valid;
    logic                    aso_ready = 1'b0;
    logic [DATA_WIDTH-1:0]   aso_data;
    logic                    aso_startofpacket, aso_endofpacket;

    dct_accel #(.NUM_ENGINES(NUM_ENGINES), .LANES(LANES), .DATA_WIDTH(DATA_WIDTH),
                .BUF_IN_WORDS(BUF_IN_WORDS), .BUF_OUT_WORDS(BUF_OUT_WORDS)) dut (
        .clk, .rst,
        .avs_csr_address, .avs_csr_read, .avs_csr_write, .avs_csr_writedata, .avs_csr_readdata,
        .avs_buf_address, .avs_buf_read, .avs_buf_write, .avs_buf_writedata,
        .avs_buf_byteenable, .avs_buf_readdata,
        .asi_valid, .asi_ready, .asi_data,
        .aso_valid, .aso_ready, .aso_data, .aso_startofpacket, .aso_endofpacket
    );

    // ---- settings ----
    int    seed = 1, nrand = 200, scale = 1, stall_in = 20, stall_out = 30;
    string logdir = "";
    int    nblocks, m1_blocks;
    longint poll_limit;

    int errors = 0;

    function automatic void fail(input string msg);
        errors++;
        if (errors <= 20) $display("ERROR: %s", msg);
    endfunction

    // =========================================================================
    // Bus tasks. Everything is driven and sampled on the falling edge.
    // =========================================================================
    task automatic csr_write(input int offset, input logic [31:0] data);
        avs_csr_address   = 8'(offset >> 2);
        avs_csr_writedata = data;
        avs_csr_write     = 1'b1;
        @(negedge clk);
        avs_csr_write     = 1'b0;
    endtask

    task automatic csr_read(input int offset, output logic [31:0] data);
        avs_csr_address = 8'(offset >> 2);
        avs_csr_read    = 1'b1;
        @(negedge clk);                 // readdata registered on this rising edge
        avs_csr_read    = 1'b0;
        data            = avs_csr_readdata;
    endtask

    task automatic csr_expect(input int offset, input logic [31:0] exp, input string what);
        logic [31:0] got;
        csr_read(offset, got);
        if (got !== exp)
            fail($sformatf("%s: read 0x%08h, expected 0x%08h", what, got, exp));
    endtask

    task automatic buf_write(input int word, input logic [DATA_WIDTH-1:0] data,
                             input logic [DATA_WIDTH/8-1:0] be);
        avs_buf_address    = BUF_AW'(word);
        avs_buf_writedata  = data;
        avs_buf_byteenable = be;
        avs_buf_write      = 1'b1;
        @(negedge clk);
        avs_buf_write      = 1'b0;
    endtask

    task automatic buf_read(input int word, output logic [DATA_WIDTH-1:0] data);
        avs_buf_address = BUF_AW'(word);
        avs_buf_read    = 1'b1;
        @(negedge clk);
        avs_buf_read    = 1'b0;
        data            = avs_buf_readdata;
    endtask

    // Poll STATUS until BUSY clears (as dctq will); returns the last STATUS.
    task automatic wait_idle(output logic [31:0] status, input string what);
        longint t0 = cycle;
        do csr_read(R_STATUS, status);
        while ((status & ST_BUSY) != 0 && cycle - t0 < poll_limit);
        if ((status & ST_BUSY) != 0)
            fail($sformatf("%s: TIMEOUT, still busy after %0d cycles", what, cycle - t0));
    endtask

    // =========================================================================
    // Helpers
    // =========================================================================
    function automatic logic [DATA_WIDTH-1:0] pixel_beat(input int b, input int beat);
        logic [DATA_WIDTH-1:0] d;
        for (int j = 0; j < PIX_PER_BEAT; j++)
            d[j*8 +: 8] = 8'(dpi_test_pixel(b, beat * PIX_PER_BEAT + j));
        return d;
    endfunction

    // Compare one output beat; returns 1 if all coefficients match.
    function automatic bit check_coef_beat(input int b, input int beat,
                                           input logic [DATA_WIDTH-1:0] d, input string what);
        bit ok = 1;
        for (int j = 0; j < COEF_PER_BEAT; j++) begin
            int idx = beat * COEF_PER_BEAT + j;
            logic signed [15:0] got = d[j*16 +: 16];
            int exp = dpi_ref_q(b, scale, idx);
            if (int'(got) != exp) begin
                if (ok)
                    fail($sformatf("%s block %0d (%s), coefficient %0d (%0d,%0d): got %0d, expected %0d",
                                   what, b, dpi_test_block_name(b), idx, idx / 8, idx % 8, got, exp));
                ok = 0;
            end
        end
        return ok;
    endfunction

    task automatic run_buf_job(input int n, input string what, output logic [31:0] cycles);
        logic [31:0] st;
        csr_write(R_NUM_BLOCKS, 32'(n));
        csr_write(R_CTRL, START | (32'd1 << 4));
        wait_idle(st, what);
        if (st[3:0] != ST_DONE[3:0])
            fail($sformatf("%s: STATUS 0x%h after job, expected DONE only", what, st));
        csr_read(R_CYCLES, cycles);
    endtask

    // Write test blocks first..first+n-1 into buffer blocks 0..n-1.
    // Every third word is written in two halves (plus one write with no byte
    // enabled, which must change nothing) to test byteenable.
    task automatic fill_buf_input(input int n, input int first);
        logic [DATA_WIDTH-1:0] d;
        for (int b = 0; b < n; b++)
            for (int beat = 0; beat < IN_BEATS; beat++) begin
                int w = b * IN_BEATS + beat;
                d = pixel_beat(first + b, beat);
                if (w % 3 == 0) begin
                    buf_write(w, d, {{(DATA_WIDTH/16){1'b0}}, {(DATA_WIDTH/16){1'b1}}});
                    buf_write(w, ~d, '0);
                    buf_write(w, d, {{(DATA_WIDTH/16){1'b1}}, {(DATA_WIDTH/16){1'b0}}});
                end else begin
                    buf_write(w, d, '1);
                end
            end
    endtask

    // Buffer block b must hold the coefficients of test block first + b.
    task automatic check_buf_output(input int n, input int first, input string what, output int bad);
        logic [DATA_WIDTH-1:0] d;
        bad = 0;
        for (int b = 0; b < n; b++) begin
            bit ok = 1;
            for (int beat = 0; beat < OUT_BEATS; beat++) begin
                buf_read(BUF_IN_WORDS + b * OUT_BEATS + beat, d);
                if (!check_coef_beat(first + b, beat, d, what)) ok = 0;
            end
            if (!ok) bad++;
        end
    endtask

    // ---- mode-2 stream driver (mSGDMA memory -> stream) ----
    task automatic m2_send(input int n);
        longint t0 = cycle;
        for (int b = 0; b < n && cycle - t0 < poll_limit; b++) begin
            for (int beat = 0; beat < IN_BEATS; beat++) begin
                while ($urandom_range(0, 99) < stall_in) begin
                    asi_valid = 1'b0;
                    @(negedge clk);
                end
                asi_valid = 1'b1;
                asi_data  = pixel_beat(b, beat);
                while (!asi_ready && cycle - t0 < poll_limit) @(negedge clk);
                @(negedge clk);
                asi_valid = 1'b0;
            end
        end
    endtask

    // ---- mode-2 stream monitor (mSGDMA stream -> memory) ----
    task automatic m2_receive(input int n, output int got_blocks, output int bad);
        int beat = 0, blk = 0;
        bit ok = 1;
        longint t0 = cycle;
        bad = 0;
        while (blk < n && cycle - t0 < poll_limit) begin
            @(negedge clk);
            aso_ready = ($urandom_range(0, 99) >= stall_out);
            if (aso_valid && aso_ready) begin
                if (!check_coef_beat(blk, beat, aso_data, "MODE 2")) ok = 0;
                if (aso_startofpacket != (blk == 0 && beat == 0) ||
                    aso_endofpacket   != (blk == n - 1 && beat == OUT_BEATS - 1))
                    fail($sformatf("MODE 2 framing: block %0d beat %0d sop %0b eop %0b",
                                   blk, beat, aso_startofpacket, aso_endofpacket));
                beat++;
                if (beat == OUT_BEATS) begin
                    if (!ok) bad++;
                    ok   = 1;
                    beat = 0;
                    blk++;
                end
            end
        end
        // The last handshake was seen at a falling edge; the transfer happens at the
        // next rising edge, so keep aso_ready up until that edge has passed.
        @(negedge clk);
        aso_ready  = 1'b0;
        got_blocks = blk;
    endtask

    // Hold asi_valid high for a few cycles and check nothing is accepted.
    task automatic expect_no_accept(input string what);
        asi_valid = 1'b1;
        asi_data  = '1;
        repeat (20) begin
            @(negedge clk);
            if (asi_ready) fail($sformatf("%s: asi_ready is high", what));
        end
        asi_valid = 1'b0;
    endtask

    // =========================================================================
    // Test sequence
    // =========================================================================
    initial begin
        logic [31:0] v, st, m0_cycles, m1_cycles, m2_cycles;
        logic [DATA_WIDTH-1:0] d;
        int    m0_bad = 0, m1_bad = 0, m2_bad = 0, m2_got = 0, bad;
        string cfg, cpb, result, details;

        void'($value$plusargs("seed=%d", seed));
        void'($value$plusargs("nrand=%d", nrand));
        void'($value$plusargs("scale=%d", scale));
        void'($value$plusargs("stall_in=%d", stall_in));
        void'($value$plusargs("stall_out=%d", stall_out));
        void'($value$plusargs("logdir=%s", logdir));
        nblocks    = dpi_test_block_count(nrand);
        m1_blocks  = (nblocks < BUF_BLOCKS) ? nblocks : BUF_BLOCKS;
        poll_limit = longint'(nblocks * (IN_BEATS + OUT_BEATS + 1024 / LANES + 16) * 20 + 2000);

        $display("tb_dct_accel: NUM_ENGINES=%0d LANES=%0d DATA_WIDTH=%0d buffer %0d blocks scale=%0d seed=%0d blocks=%0d stall %0d%%/%0d%%",
                 NUM_ENGINES, LANES, DATA_WIDTH, BUF_BLOCKS, scale, seed, nblocks, stall_in, stall_out);

        repeat (5) @(negedge clk);
        rst = 1'b0;
        @(negedge clk);

        // ---- 1. registers ----
        csr_expect(R_STATUS, 32'h0, "STATUS after reset");
        csr_expect(R_CTRL, 32'h0, "CTRL after reset");
        csr_expect(R_INFO, {4'd1, 12'(DATA_WIDTH), 8'(LANES), 8'(NUM_ENGINES)}, "INFO");
        csr_expect(R_MAGIC, 32'h4443_5431, "MAGIC");
        csr_expect(R_BUF_BLOCKS, 32'(BUF_BLOCKS), "BUF_BLOCKS");
        csr_write(R_SCRATCH, 32'hA5C3_0F96);
        csr_expect(R_SCRATCH, 32'hA5C3_0F96, "SCRATCH");
        csr_write(R_SCRATCH, 32'h5A3C_F069);
        csr_expect(R_SCRATCH, 32'h5A3C_F069, "SCRATCH");

        for (int i = 0; i < 64; i++)
            csr_write(R_QTAB + 4 * i, 32'(dpi_recip(scale, i)));
        for (int i = 0; i < 64; i++)
            csr_expect(R_QTAB + 4 * i, 32'(dpi_recip(scale, i)), $sformatf("Q_TABLE[%0d]", i));

        // ---- 2. ERROR cases (nothing may start) ----
        csr_write(R_NUM_BLOCKS, 32'd5);
        csr_write(R_CTRL, START | (32'd3 << 4));
        csr_expect(R_STATUS, ST_ERROR, "STATUS after START with MODE 3");
        csr_write(R_NUM_BLOCKS, 32'd0);
        csr_write(R_CTRL, START | (32'd1 << 4));
        csr_expect(R_STATUS, ST_ERROR, "STATUS after START with NUM_BLOCKS 0");
        csr_write(R_NUM_BLOCKS, 32'(BUF_BLOCKS + 1));
        csr_write(R_CTRL, START | (32'd1 << 4));
        csr_expect(R_STATUS, ST_ERROR, "STATUS after START with NUM_BLOCKS > BUF_BLOCKS");
        csr_write(R_CTRL, SOFT_RESET);
        csr_expect(R_STATUS, 32'h0, "STATUS after SOFT_RESET");

        // ---- 3. MODE 0: register block ----
        for (int b = 0; b < nblocks; b++) begin
            bit ok = 1;
            for (int i = 0; i < 64; i++)
                csr_write(R_IN_BLOCK + 4 * i, 32'(dpi_test_pixel(b, i)));
            if (b == 0)
                for (int i = 0; i < 64; i++)
                    csr_expect(R_IN_BLOCK + 4 * i, 32'(dpi_test_pixel(b, i)), "IN_BLOCK readback");
            csr_write(R_CTRL, START);
            wait_idle(st, "MODE 0");
            if (st[3:0] != ST_DONE[3:0])
                fail($sformatf("MODE 0 block %0d: STATUS 0x%h, expected DONE only", b, st));
            if (b == 0) csr_read(R_CYCLES, m0_cycles);
            for (int i = 0; i < 64; i++) begin
                int exp = dpi_ref_q(b, scale, i);
                csr_read(R_OUT_BLOCK + 4 * i, v);
                if (v !== 32'(exp)) begin            // sign-extended in the register
                    if (ok)
                        fail($sformatf("MODE 0 block %0d (%s), coefficient %0d: read 0x%08h, expected %0d",
                                       b, dpi_test_block_name(b), i, v, exp));
                    ok = 0;
                end
            end
            if (!ok) m0_bad++;
        end
        $display("MODE 0: %0d blocks, %0d bad, %0d cycles START->DONE", nblocks, m0_bad, m0_cycles);

        // ---- 4. MODE 1: internal buffer ----
        fill_buf_input(m1_blocks, 0);
        for (int w = 0; w < 4; w++) begin
            buf_read(w, d);
            if (d !== pixel_beat(w / IN_BEATS, w % IN_BEATS))
                fail($sformatf("buffer input readback word %0d: 0x%h", w, d));
        end
        run_buf_job(m1_blocks, "MODE 1", m1_cycles);
        check_buf_output(m1_blocks, 0, "MODE 1", m1_bad);
        $display("MODE 1: %0d blocks, %0d bad, %0d cycles (%0.1f cycles/block)", m1_blocks, m1_bad,
                 m1_cycles, real'(m1_cycles) / real'(m1_blocks));

        // ---- 5. START while busy, SOFT_RESET mid-job, recovery ----
        csr_write(R_NUM_BLOCKS, 32'(m1_blocks));
        csr_write(R_CTRL, START | (32'd1 << 4));
        repeat (30) @(negedge clk);
        csr_write(R_CTRL, START | (32'd1 << 4));
        csr_read(R_STATUS, v);
        if ((v & 32'h7) != (ST_BUSY | ST_ERROR))
            fail($sformatf("STATUS after START while busy: 0x%h, expected BUSY|ERROR", v));
        csr_write(R_CTRL, SOFT_RESET);
        csr_expect(R_STATUS, 32'h0, "STATUS after SOFT_RESET mid-job");
        csr_write(R_CTRL, SOFT_RESET);                       // again, while idle
        csr_expect(R_CTRL, 32'h10, "MODE kept after SOFT_RESET");
        csr_expect(R_QTAB + 4 * 5, 32'(dpi_recip(scale, 5)), "Q_TABLE kept after SOFT_RESET");
        // Different blocks this time, so stale output from the run above can't pass.
        fill_buf_input(4, m1_blocks - 4);
        run_buf_job(4, "MODE 1 after SOFT_RESET", v);
        check_buf_output(4, m1_blocks - 4, "MODE 1 after SOFT_RESET", bad);
        m1_bad += bad;

        // ---- 6. MODE 2: external streams ----
        csr_write(R_NUM_BLOCKS, 32'(nblocks));
        expect_no_accept("MODE 2 before START");
        csr_write(R_CTRL, START | (32'd2 << 4));
        csr_expect(R_CTRL, 32'h20, "CTRL readback (MODE 2)");
        fork
            m2_send(nblocks);
            m2_receive(nblocks, m2_got, m2_bad);
        join
        wait_idle(st, "MODE 2");
        if (st[3:0] != ST_DONE[3:0])
            fail($sformatf("MODE 2: STATUS 0x%h after job, expected DONE only", st));
        if (m2_got != nblocks)
            fail($sformatf("MODE 2: TIMEOUT, %0d of %0d blocks received", m2_got, nblocks));
        csr_read(R_CYCLES, m2_cycles);
        expect_no_accept("MODE 2 after the job");
        $display("MODE 2: %0d blocks, %0d bad, %0d cycles (%0.1f cycles/block)", m2_got, m2_bad,
                 m2_cycles, real'(m2_cycles) / real'(nblocks));

        // ---- result ----
        cfg = $sformatf("dct_accel NUM_ENGINES=%0d LANES=%0d DATA_WIDTH=%0d scale=%0d stall=%0d/%0d",
                        NUM_ENGINES, LANES, DATA_WIDTH, scale, stall_in, stall_out);
        cpb = $sformatf("%0.1f", real'(m1_cycles) / real'(m1_blocks));
        details = $sformatf("reg %0d blk (%0d cyc/START), buf %0d blk, stream %0d blk (%0.1f cyc/blk)",
                            nblocks, m0_cycles, m1_blocks, nblocks, real'(m2_cycles) / real'(nblocks));
        if (errors != 0) begin
            result  = "FAIL";
            details = $sformatf("%0d errors (bad blocks: reg %0d, buf %0d, stream %0d); %s",
                                errors, m0_bad, m1_bad, m2_bad, details);
        end else begin
            result  = "PASS";
        end
        $display("%s: %s (buffer mode %s cycles/block)", result, details, cpb);
        if (logdir != "")
            dpi_log_sim(logdir, "dct_accel", cfg, seed, nblocks, cpb, result, details);
        $finish;
    end

endmodule

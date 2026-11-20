// =============================================================================
// dct_accel.sv - the accelerator as seen by the HPS: registers, data-path
// feeders and the multi-engine array.
//
//                 ┌───────────────────────────── dct_accel ─────────────────────────────┐
//   CSR slave ───►│ registers (CTRL, STATUS, NUM_BLOCKS, CYCLE_COUNT, INFO, Q_TABLE, ...) │
//  (LW bridge)    │                                                                      │
//                 │  MODE 0: IN_BLOCK regs ──┐                    ┌──► OUT_BLOCK regs     │
//   buffer slave ►│  MODE 1: in_ram ─────────┼─► dct_array ───────┼──► out_ram  ◄─────────│◄ buffer slave
//  (main bridge)  │  MODE 2: asi_* stream ───┘   (N engines)      └──► aso_* stream        │
//                 └──────────────────────────────────────────────────────────────────────┘
//
// MODE 0 (option 1): the HPS writes 64 pixels into IN_BLOCK, START, polls
//                    STATUS, reads 64 coefficients from OUT_BLOCK. One block per START.
// MODE 1 (option 2): the HPS memcpy's pixels into the input buffer (offset 0 of
//                    the buffer slave), START, polls, memcpy's the coefficients
//                    from the output buffer (byte offset BUF_IN_WORDS*DATA_WIDTH/8).
// MODE 2 (options 3/4): pixels arrive on the asi_* Avalon-ST sink (mSGDMA
//                    memory->stream), coefficients leave on aso_* (mSGDMA
//                    stream->memory). Exactly NUM_BLOCKS blocks are accepted.
//
// Register map (CSR slave, 32-bit registers, byte offsets; see FPGA/DESIGN.md §10):
//   0x000 CTRL        bit 0 START (self-clearing), bit 1 SOFT_RESET (self-clearing),
//                     bits 5:4 MODE
//   0x004 STATUS      bit 0 BUSY, bit 1 DONE, bit 2 ERROR, bit 3 ENGINES_BUSY
//   0x008 NUM_BLOCKS  blocks per START (modes 1, 2; mode 0 always does 1)
//   0x00C CYCLE_COUNT cycles from START to DONE
//   0x010 INFO        [7:0] NUM_ENGINES, [15:8] LANES, [27:16] DATA_WIDTH, [31:28] VERSION
//   0x014 BUF_BLOCKS  capacity of the mode-1 buffer in blocks
//   0x018 MAGIC       0x44435431 ("DCT1")
//   0x01C SCRATCH     read/write, no effect (bus test)
//   0x100-0x1FC Q_TABLE   64 reciprocals R (17 bit)
//   0x200-0x2FC IN_BLOCK  64 pixels, one per word (bits 7:0)
//   0x300-0x3FC OUT_BLOCK 64 coefficients, one per word, sign-extended
//
// Both slaves have a fixed read latency of 1 cycle and no wait states.
// =============================================================================
module dct_accel #(
    parameter int NUM_ENGINES   = 1,
    parameter int LANES         = 8,
    parameter int DATA_WIDTH    = 64,
    parameter int BUF_IN_WORDS  = 8192,    // mode-1 input buffer, DATA_WIDTH-bit words (64 KB)
    parameter int BUF_OUT_WORDS = 16384    // mode-1 output buffer (128 KB)
) (
    input  logic                    clk,
    input  logic                    rst,            // synchronous, active high

    // CSR slave (lightweight HPS-to-FPGA bridge): word addresses
    input  logic [7:0]              avs_csr_address,
    input  logic                    avs_csr_read,
    input  logic                    avs_csr_write,
    input  logic [31:0]             avs_csr_writedata,
    output logic [31:0]             avs_csr_readdata,

    // Buffer slave (main HPS-to-FPGA bridge): word addresses
    input  logic [$clog2(BUF_IN_WORDS + BUF_OUT_WORDS)-1:0] avs_buf_address,
    input  logic                    avs_buf_read,
    input  logic                    avs_buf_write,
    input  logic [DATA_WIDTH-1:0]   avs_buf_writedata,
    input  logic [DATA_WIDTH/8-1:0] avs_buf_byteenable,
    output logic [DATA_WIDTH-1:0]   avs_buf_readdata,

    // Avalon-ST sink: pixels from the mSGDMA (mode 2)
    input  logic                    asi_valid,
    output logic                    asi_ready,
    input  logic [DATA_WIDTH-1:0]   asi_data,

    // Avalon-ST source: coefficients to the mSGDMA (mode 2)
    output logic                    aso_valid,
    input  logic                    aso_ready,
    output logic [DATA_WIDTH-1:0]   aso_data,
    output logic                    aso_startofpacket,   // first beat of the job
    output logic                    aso_endofpacket      // last beat of the job
);

    localparam int PIX_PER_BEAT  = DATA_WIDTH / 8;
    localparam int COEF_PER_BEAT = DATA_WIDTH / 16;
    localparam int IN_BEATS      = 64 / PIX_PER_BEAT;
    localparam int OUT_BEATS     = 64 / COEF_PER_BEAT;

    localparam int IN_AW   = $clog2(BUF_IN_WORDS);
    localparam int OUT_AW  = $clog2(BUF_OUT_WORDS);
    localparam int BUF_AW  = $clog2(BUF_IN_WORDS + BUF_OUT_WORDS);
    localparam int CAP_IN  = BUF_IN_WORDS / IN_BEATS;
    localparam int CAP_OUT = BUF_OUT_WORDS / OUT_BEATS;
    localparam int BUF_BLOCKS = (CAP_IN < CAP_OUT) ? CAP_IN : CAP_OUT;

    localparam logic [31:0] MAGIC   = 32'h4443_5431;   // "DCT1"
    localparam logic [3:0]  VERSION = 4'd1;
    localparam logic [31:0] INFO    = {VERSION, 12'(DATA_WIDTH), 8'(LANES), 8'(NUM_ENGINES)};

    localparam logic [1:0] M_REG = 2'd0, M_BUF = 2'd1, M_STREAM = 2'd2;

    // -------------------------------------------------------------------------
    // CSR decode
    // -------------------------------------------------------------------------
    logic [1:0] csr_region;   // 0 = control regs, 1 = Q_TABLE, 2 = IN_BLOCK, 3 = OUT_BLOCK
    logic [5:0] csr_idx;
    assign csr_region = avs_csr_address[7:6];
    assign csr_idx    = avs_csr_address[5:0];

    logic ctrl_wr;
    assign ctrl_wr = avs_csr_write && (csr_region == 2'd0) && (csr_idx == 6'd0);

    // Settings (reset only by the hardware reset)
    logic [1:0]  mode;
    logic [31:0] num_blocks;
    logic [31:0] scratch;
    logic        soft_rst;    // SOFT_RESET write: resets the run state in the same cycle

    // Run state (also cleared by SOFT_RESET)
    logic        core_rst;
    logic        running, done, error;
    logic [31:0] cycle_count;
    logic [1:0]  run_mode;    // mode latched at START
    logic        primed;      // mode-1 read pipeline holds valid data

    // Storage (not reset: always written before use)
    logic [16:0] q_shadow [0:63];   // readback copy of the Q table
    logic [7:0]  in_blk   [0:63];   // IN_BLOCK  (mode 0)
    logic [15:0] out_blk  [0:63];   // OUT_BLOCK (mode 0)

    // SOFT_RESET acts on the write itself, so the next STATUS read already shows
    // idle. Settings, Q table and buffers are kept.
    assign soft_rst = ctrl_wr && avs_csr_writedata[1];
    assign core_rst = rst || soft_rst;

    // START request and its validity
    logic        start_req;
    logic [1:0]  start_mode;
    logic        start_ok;
    logic [31:0] start_blocks;

    assign start_req    = ctrl_wr && avs_csr_writedata[0] && !avs_csr_writedata[1];
    assign start_mode   = avs_csr_writedata[5:4];
    assign start_blocks = (start_mode == M_REG) ? 32'd1 : num_blocks;

    always_comb begin
        case (start_mode)
            M_REG:    start_ok = 1'b1;
            M_BUF:    start_ok = (num_blocks != 32'd0) && (num_blocks <= 32'(BUF_BLOCKS));
            M_STREAM: start_ok = (num_blocks != 32'd0);
            default:  start_ok = 1'b0;
        endcase
    end

    // -------------------------------------------------------------------------
    // Engine array
    // -------------------------------------------------------------------------
    logic                  arr_in_valid, arr_in_ready;
    logic [DATA_WIDTH-1:0] arr_in_data;
    logic                  arr_out_valid, arr_out_ready;
    logic [DATA_WIDTH-1:0] arr_out_data;
    logic                  arr_out_sop, arr_out_eop, arr_busy;

    dct_array #(.NUM_ENGINES(NUM_ENGINES), .LANES(LANES), .DATA_WIDTH(DATA_WIDTH)) u_array (
        .clk       (clk),
        .rst       (core_rst),
        .q_we      (avs_csr_write && (csr_region == 2'd1)),
        .q_addr    (csr_idx),
        .q_data    (avs_csr_writedata[16:0]),
        .in_valid  (arr_in_valid),
        .in_ready  (arr_in_ready),
        .in_data   (arr_in_data),
        .out_valid (arr_out_valid),
        .out_ready (arr_out_ready),
        .out_data  (arr_out_data),
        .out_sop   (arr_out_sop),
        .out_eop   (arr_out_eop),
        .busy      (arr_busy)
    );

    // -------------------------------------------------------------------------
    // Input side: counts beats/blocks and selects the pixel source
    // -------------------------------------------------------------------------
    logic [31:0]       in_left;     // blocks still to be accepted
    logic [6:0]        in_beat;     // beat within the current block
    logic [IN_AW-1:0]  in_addr;     // mode 1: input buffer word being sent
    logic              in_more;
    logic              in_fire;

    assign in_more = running && (in_left != 32'd0);

    logic [DATA_WIDTH-1:0] reg_data;      // mode 0: beat built from IN_BLOCK
    logic [DATA_WIDTH-1:0] in_ram_rdata;  // mode 1: beat read from in_ram

    for (genvar j = 0; j < PIX_PER_BEAT; j++) begin : g_reg_in
        assign reg_data[j*8 +: 8] = in_blk[6'((int'(in_beat) * PIX_PER_BEAT) + j)];
    end

    always_comb begin
        case (run_mode)
            M_STREAM: begin
                arr_in_valid = in_more && asi_valid;
                arr_in_data  = asi_data;
            end
            M_BUF: begin
                arr_in_valid = in_more && primed;
                arr_in_data  = in_ram_rdata;
            end
            default: begin
                arr_in_valid = in_more && primed;
                arr_in_data  = reg_data;
            end
        endcase
    end

    assign in_fire   = arr_in_valid && arr_in_ready;
    assign asi_ready = (run_mode == M_STREAM) && in_more && arr_in_ready;

    // -------------------------------------------------------------------------
    // Output side: modes 0/1 always accept; mode 2 follows the stream sink
    // -------------------------------------------------------------------------
    logic [31:0]       out_left;    // blocks still to be delivered
    logic              out_first;   // next beat is the first of the job
    logic [OUT_AW-1:0] out_addr;    // output word being written (modes 0, 1)
    logic              out_fire;

    assign arr_out_ready = running && ((run_mode == M_STREAM) ? aso_ready : 1'b1);
    assign out_fire      = arr_out_valid && arr_out_ready;

    assign aso_valid         = (run_mode == M_STREAM) && running && arr_out_valid;
    assign aso_data          = arr_out_data;
    assign aso_startofpacket = arr_out_sop && out_first;
    assign aso_endofpacket   = arr_out_eop && (out_left == 32'd1);

    // -------------------------------------------------------------------------
    // Run control
    // -------------------------------------------------------------------------
    always_ff @(posedge clk) begin
        if (core_rst) begin
            running     <= 1'b0;
            done        <= 1'b0;
            error       <= 1'b0;
            cycle_count <= '0;
            run_mode    <= M_REG;
            primed      <= 1'b0;
            in_left     <= '0;
            in_beat     <= '0;
            in_addr     <= '0;
            out_left    <= '0;
            out_first   <= 1'b0;
            out_addr    <= '0;
        end else if (start_req && !running) begin
            if (start_ok) begin
                running     <= 1'b1;
                done        <= 1'b0;
                error       <= 1'b0;
                cycle_count <= '0;
                run_mode    <= start_mode;
                primed      <= 1'b0;
                in_left     <= start_blocks;
                in_beat     <= '0;
                in_addr     <= '0;
                out_left    <= start_blocks;
                out_first   <= 1'b1;
                out_addr    <= '0;
            end else begin
                done  <= 1'b0;
                error <= 1'b1;        // invalid MODE or NUM_BLOCKS: nothing starts
            end
        end else begin
            if (start_req)
                error <= 1'b1;        // START while BUSY: ignored, flagged

            if (running) begin
                cycle_count <= cycle_count + 32'd1;
                primed      <= 1'b1;  // in_ram output is valid from the 2nd cycle

                if (in_fire) begin
                    in_addr <= in_addr + IN_AW'(1);
                    if (in_beat == 7'(IN_BEATS - 1)) begin
                        in_beat <= '0;
                        in_left <= in_left - 32'd1;
                    end else begin
                        in_beat <= in_beat + 7'd1;
                    end
                end

                if (out_fire) begin
                    out_addr  <= out_addr + OUT_AW'(1);
                    out_first <= 1'b0;
                    if (arr_out_eop) begin
                        out_left <= out_left - 32'd1;
                        if (out_left == 32'd1) begin
                            running <= 1'b0;
                            done    <= 1'b1;
                        end
                    end
                end
            end
        end
    end

    // -------------------------------------------------------------------------
    // Settings registers
    // -------------------------------------------------------------------------
    always_ff @(posedge clk) begin
        if (rst) begin
            mode       <= M_REG;
            num_blocks <= '0;
            scratch    <= '0;
        end else begin
            if (avs_csr_write && csr_region == 2'd0) begin
                case (csr_idx)
                    6'd0: if (!running && !soft_rst) mode <= avs_csr_writedata[5:4];
                    6'd2: if (!running) num_blocks <= avs_csr_writedata;
                    6'd7:               scratch    <= avs_csr_writedata;
                    default: ;
                endcase
            end
        end
    end

    // Q table shadow and IN_BLOCK (written by the HPS)
    always_ff @(posedge clk) begin
        if (avs_csr_write && csr_region == 2'd1)
            q_shadow[csr_idx] <= avs_csr_writedata[16:0];
        if (avs_csr_write && csr_region == 2'd2)
            in_blk[csr_idx] <= avs_csr_writedata[7:0];
    end

    // OUT_BLOCK (written by the collector in mode 0; one block, so out_addr < OUT_BEATS)
    always_ff @(posedge clk) begin
        if (out_fire && run_mode == M_REG)
            for (int j = 0; j < COEF_PER_BEAT; j++)
                out_blk[6'((int'(out_addr) * COEF_PER_BEAT) + j)] <= arr_out_data[j*16 +: 16];
    end

    // -------------------------------------------------------------------------
    // CSR read (latency 1)
    // -------------------------------------------------------------------------
    always_ff @(posedge clk) begin
        if (avs_csr_read) begin
            case (csr_region)
                2'd0: begin
                    case (csr_idx)
                        6'd0:    avs_csr_readdata <= {26'd0, mode, 4'd0};
                        6'd1:    avs_csr_readdata <= {28'd0, arr_busy, error, done, running};
                        6'd2:    avs_csr_readdata <= num_blocks;
                        6'd3:    avs_csr_readdata <= cycle_count;
                        6'd4:    avs_csr_readdata <= INFO;
                        6'd5:    avs_csr_readdata <= 32'(BUF_BLOCKS);
                        6'd6:    avs_csr_readdata <= MAGIC;
                        6'd7:    avs_csr_readdata <= scratch;
                        default: avs_csr_readdata <= 32'd0;
                    endcase
                end
                2'd1:    avs_csr_readdata <= {15'd0, q_shadow[csr_idx]};
                2'd2:    avs_csr_readdata <= {24'd0, in_blk[csr_idx]};
                default: avs_csr_readdata <= {{16{out_blk[csr_idx][15]}}, out_blk[csr_idx]};
            endcase
        end
    end

    // -------------------------------------------------------------------------
    // Mode-1 buffers and the buffer slave
    //   word 0 .. BUF_IN_WORDS-1                 : input pixels  (HPS writes; reads back when idle)
    //   word BUF_IN_WORDS .. +BUF_OUT_WORDS-1    : output coefficients (HPS reads; writes ignored)
    // While a mode-1 job runs, the input buffer's read port belongs to the feeder.
    // -------------------------------------------------------------------------
    logic              buf_is_in;
    logic              buf_sel_out_q;   // registered: which RAM the pending read used
    logic [IN_AW-1:0]  in_ram_raddr;
    logic [OUT_AW-1:0] out_ram_raddr;
    logic [DATA_WIDTH-1:0] out_ram_rdata;

    /* verilator lint_off UNUSEDSIGNAL */
    logic [BUF_AW-1:0] buf_out_word;    // only the low OUT_AW bits are used
    /* verilator lint_on UNUSEDSIGNAL */

    assign buf_is_in     = (avs_buf_address < BUF_AW'(BUF_IN_WORDS));
    assign buf_out_word  = avs_buf_address - BUF_AW'(BUF_IN_WORDS);
    assign out_ram_raddr = buf_out_word[OUT_AW-1:0];

    // Feeder read address runs one word ahead when a beat is taken, so the RAM
    // output always holds the beat at in_addr.
    assign in_ram_raddr = (running && run_mode == M_BUF)
                          ? (in_fire ? in_addr + IN_AW'(1) : in_addr)
                          : avs_buf_address[IN_AW-1:0];

    dct_ram #(.WIDTH(DATA_WIDTH), .DEPTH(BUF_IN_WORDS)) u_in_ram (
        .clk   (clk),
        .we    (avs_buf_write && buf_is_in),
        .be    (avs_buf_byteenable),
        .waddr (avs_buf_address[IN_AW-1:0]),
        .wdata (avs_buf_writedata),
        .raddr (in_ram_raddr),
        .rdata (in_ram_rdata)
    );

    dct_ram #(.WIDTH(DATA_WIDTH), .DEPTH(BUF_OUT_WORDS)) u_out_ram (
        .clk   (clk),
        .we    (out_fire && run_mode == M_BUF),
        .be    ({(DATA_WIDTH/8){1'b1}}),
        .waddr (out_addr),
        .wdata (arr_out_data),
        .raddr (out_ram_raddr),
        .rdata (out_ram_rdata)
    );

    always_ff @(posedge clk) begin
        if (avs_buf_read)
            buf_sel_out_q <= !buf_is_in;
    end

    assign avs_buf_readdata = buf_sel_out_q ? out_ram_rdata : in_ram_rdata;

endmodule

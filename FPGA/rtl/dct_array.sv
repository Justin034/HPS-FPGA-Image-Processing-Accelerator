// =============================================================================
// dct_array.sv - NUM_ENGINES dct_engine instances behind one stream interface.
//
//   in stream ──► distributor ──► engine 0 ──► collector ──► out stream
//                 (round-robin)    engine 1      (same order)
//                                  ...
//
// Distributor: whole blocks go to engines in turn (block i -> engine i mod N).
// Collector:   takes whole blocks from the engines in the same order, so the
//              output is in the original block order.
// This works because every block takes exactly the same number of cycles and
// each engine holds one block at a time.
//
// The Q table write port is broadcast to every engine.
// Same ports and data layout as dct_engine, so it is a drop-in replacement.
//
// Throughput (DATA_WIDTH = 64, LANES = 8, no backpressure):
//   about max(152 / NUM_ENGINES, 16) cycles per block; 16 = output beats per block.
//
// See FPGA/DESIGN.md, section "dct_array".
// =============================================================================
module dct_array #(
    parameter int NUM_ENGINES = 4,     // 1..16
    parameter int LANES       = 8,
    parameter int DATA_WIDTH  = 64
) (
    input  logic                  clk,
    input  logic                  rst,

    input  logic                  q_we,
    input  logic [5:0]            q_addr,
    input  logic [16:0]           q_data,

    input  logic                  in_valid,
    output logic                  in_ready,
    input  logic [DATA_WIDTH-1:0] in_data,

    output logic                  out_valid,
    input  logic                  out_ready,
    output logic [DATA_WIDTH-1:0] out_data,
    output logic                  out_sop,
    output logic                  out_eop,

    output logic                  busy
);

    localparam int IN_BEATS = 64 / (DATA_WIDTH / 8);
    localparam int SEL_W    = (NUM_ENGINES > 1) ? $clog2(NUM_ENGINES) : 1;

    // Per-engine signals
    logic [NUM_ENGINES-1:0] eng_in_valid, eng_in_ready;
    logic [NUM_ENGINES-1:0] eng_out_valid, eng_out_ready, eng_out_sop, eng_out_eop, eng_busy;
    logic [DATA_WIDTH-1:0]  eng_out_data [0:NUM_ENGINES-1];

    logic [SEL_W-1:0] in_sel;     // engine receiving the current input block
    logic [SEL_W-1:0] out_sel;    // engine whose block is being sent out
    logic [6:0]       in_cnt;     // beats of the current input block

    for (genvar e = 0; e < NUM_ENGINES; e++) begin : g_eng
        dct_engine #(.LANES(LANES), .DATA_WIDTH(DATA_WIDTH)) u_eng (
            .clk       (clk),
            .rst       (rst),
            .q_we      (q_we),
            .q_addr    (q_addr),
            .q_data    (q_data),
            .in_valid  (eng_in_valid[e]),
            .in_ready  (eng_in_ready[e]),
            .in_data   (in_data),
            .out_valid (eng_out_valid[e]),
            .out_ready (eng_out_ready[e]),
            .out_data  (eng_out_data[e]),
            .out_sop   (eng_out_sop[e]),
            .out_eop   (eng_out_eop[e]),
            .busy      (eng_busy[e])
        );

        assign eng_in_valid[e]  = in_valid  && (in_sel  == SEL_W'(e));
        assign eng_out_ready[e] = out_ready && (out_sel == SEL_W'(e));
    end

    // Stream muxes
    assign in_ready  = eng_in_ready[in_sel];
    assign out_valid = eng_out_valid[out_sel];
    assign out_data  = eng_out_data[out_sel];
    assign out_sop   = eng_out_sop[out_sel];
    assign out_eop   = eng_out_eop[out_sel];
    assign busy      = (|eng_busy) || (in_cnt != 7'd0);

    // Round-robin pointers: advance after a whole block
    always_ff @(posedge clk) begin
        if (rst) begin
            in_sel  <= '0;
            out_sel <= '0;
            in_cnt  <= '0;
        end else begin
            if (in_valid && in_ready) begin
                if (in_cnt == 7'(IN_BEATS - 1)) begin
                    in_cnt <= '0;
                    in_sel <= (in_sel == SEL_W'(NUM_ENGINES - 1)) ? '0 : in_sel + SEL_W'(1);
                end else begin
                    in_cnt <= in_cnt + 7'd1;
                end
            end
            if (out_valid && out_ready && out_eop)
                out_sel <= (out_sel == SEL_W'(NUM_ENGINES - 1)) ? '0 : out_sel + SEL_W'(1);
        end
    end

endmodule

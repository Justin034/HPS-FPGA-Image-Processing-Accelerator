// =============================================================================
// dct_ram.sv - simple dual-port RAM: one write port (with byte enables), one
// read port with a registered output (read latency 1). Single clock.
//
// Written in the form Quartus infers as M10K block RAM (byte-enabled simple
// dual-port template); Verilator simulates it directly. Contents are not reset.
// Read-during-write to the same address returns the old data.
//
// Used by dct_accel for the option-2 input and output buffers.
// =============================================================================
module dct_ram #(
    parameter int WIDTH = 64,      // multiple of 8
    parameter int DEPTH = 8192     // power of two
) (
    input  logic                     clk,

    input  logic                     we,
    input  logic [WIDTH/8-1:0]       be,
    input  logic [$clog2(DEPTH)-1:0] waddr,
    input  logic [WIDTH-1:0]         wdata,

    input  logic [$clog2(DEPTH)-1:0] raddr,
    output logic [WIDTH-1:0]         rdata
);

    localparam int NB = WIDTH / 8;

    logic [NB-1:0][7:0] mem [0:DEPTH-1];

    always_ff @(posedge clk) begin
        if (we)
            for (int i = 0; i < NB; i++)
                if (be[i])
                    mem[waddr][i] <= wdata[i*8 +: 8];
        rdata <= mem[raddr];
    end

endmodule

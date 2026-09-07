module Matrix_Mult (
    input  logic        CLOCK_50,
    input  logic [3:0]  KEY,
    output logic [9:0]  LEDR
);

    logic [25:0] counter;

    always_ff @(posedge CLOCK_50) begin
        if (!KEY[0]) begin
            counter <= 26'd0;
        end else begin
            counter <= counter + 1'b1;
        end
    end

    assign LEDR = counter[25:16];

endmodule

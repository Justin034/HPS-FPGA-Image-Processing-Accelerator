module Matrix_Mult (
    input  logic [9:0] SW,
    output logic [9:0] LEDR
);

    // Switch 0 directly controls LED 0
    assign LEDR[0] = SW[0];

    // Turn off the remaining LEDs
    assign LEDR[9:1] = 9'b0;

endmodule
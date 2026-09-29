quit -sim

# Compile design
vlog -sv ../rtl/matrix_mult_8x8.sv

# Compile testbench
vlog -sv ../tb/matrix_mult_8x8_tb.sv

# Start simulation
vsim work.matrix_mult_8x8_tb

# Add useful signals
add wave -divider "Control"
add wave sim:/matrix_mult_8x8_tb/clk
add wave sim:/matrix_mult_8x8_tb/rst
add wave sim:/matrix_mult_8x8_tb/start
add wave sim:/matrix_mult_8x8_tb/busy
add wave sim:/matrix_mult_8x8_tb/done

# Run
run -all

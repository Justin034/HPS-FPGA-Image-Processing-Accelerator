quit -sim

# Compile design
vlog -sv ../rtl/matrix_mult_8x8.sv

# Compile testbench
vlog -sv ../tb/matrix_mult_8x8_tb.sv

# Start simulation
vsim work.tb_matrix_mult_8x8

# Add useful signals
add wave -divider "Control"
add wave sim:/tb_matrix_mult_8x8/clk
add wave sim:/tb_matrix_mult_8x8/rst
add wave sim:/tb_matrix_mult_8x8/start
add wave sim:/tb_matrix_mult_8x8/busy
add wave sim:/tb_matrix_mult_8x8/done

# Run
run -all

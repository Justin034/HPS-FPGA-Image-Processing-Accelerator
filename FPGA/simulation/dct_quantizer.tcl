quit -sim

# Compile design
vlog -sv ../rtl/dct_quantizer.sv

# Compile testbench
vlog -sv ../tb/tb_dct_quantizer.sv

# Start simulation
vsim work.tb_dct_quantizer

# Add useful signals
add wave -divider "Control"
add wave sim:/tb_dct_quantizer/clk
add wave sim:/tb_dct_quantizer/rst
add wave sim:/tb_dct_quantizer/start
add wave sim:/tb_dct_quantizer/pixel
add wave sim:/tb_dct_quantizer/qcoeff
add wave sim:/tb_dct_quantizer/done

# Run
run -all

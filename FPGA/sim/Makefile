# FPGA/sim/Makefile - Verilator lint + simulation.
#
#   make                lint, engine, array and accelerator tests (all seeds + one timing run each)
#   make lint           strict lint of the RTL (-Wall)
#   make engine         engine tests only
#   make array          array tests only
#   make accel          accelerator (dct_accel) tests only
#   make synth          Yosys: check that dct_accel synthesises (small buffers)
#   make clean
#
# Options (override on the command line):
#   LANES_LIST="1 8"    LANES values to test (engine)
#   ENGINES_LIST="1 2 4" NUM_ENGINES values to test (array, LANES=8)
#   ACCEL_ENGINES_LIST="1 2" NUM_ENGINES values to test (dct_accel)
#   SEEDS="1 2 3"       random seeds (backpressure + random initial register values)
#   NRAND=200           random blocks after the 79 edge cases
#   SCALE=1             Q table scale
#   LOGDIR=...          where sim.md / sim.csv are appended (default docs/logs)
#
# Usually run from the top level: make sim

ROOT      := $(abspath ../..)
RTL       := $(ROOT)/FPGA/rtl
TB        := $(ROOT)/FPGA/tb
HPS_INC   := $(ROOT)/HPS/include
HPS_SRC   := $(ROOT)/HPS/src
BUILD     := build

VERILATOR ?= verilator
LANES_LIST ?= 1 8
ENGINES_LIST ?= 1 2 4
ACCEL_ENGINES_LIST ?= 1 2
SEEDS     ?= 1 2 3
YOSYS     ?= yosys
NRAND     ?= 200
SCALE     ?= 1
LOGDIR    ?= $(ROOT)/docs/logs

# C reference compiled into every testbench (through DPI-C)
REF_C     := $(HPS_SRC)/dct_ref.c $(HPS_SRC)/test_blocks.c $(HPS_SRC)/reslog.c $(TB)/dpi_ref.c

# --x-assign/--x-initial unique + "+verilator+rand+reset+2" at run time give every
# register a random start value (per seed), so a missing reset makes the test fail.
VFLAGS    := --binary --timing -Wno-fatal --x-assign unique --x-initial unique \
             -CFLAGS -I$(HPS_INC) -LDFLAGS -lm

RUNFLAGS  := +verilator+rand+reset+2 +nrand=$(NRAND) +scale=$(SCALE) +logdir=$(LOGDIR)

ENGINE_RTL := $(RTL)/dct_engine.sv
ARRAY_RTL  := $(RTL)/dct_engine.sv $(RTL)/dct_array.sv
ACCEL_RTL  := $(ARRAY_RTL) $(RTL)/dct_ram.sv $(RTL)/dct_accel.sv

.PHONY: all lint engine array accel synth clean

all: lint engine array accel

lint:
	$(VERILATOR) --lint-only -Wall --top-module dct_engine $(ENGINE_RTL)
	$(VERILATOR) --lint-only -Wall --top-module dct_array $(ARRAY_RTL)
	$(VERILATOR) --lint-only -Wall --top-module dct_accel $(ACCEL_RTL)
	@echo "lint: OK"

engine: $(foreach L,$(LANES_LIST),engine_L$(L))

# Build once per LANES value, then run every seed with backpressure, plus one run
# without backpressure to measure cycles per block.
engine_L%: $(ENGINE_RTL) $(TB)/tb_dct_engine.sv $(REF_C)
	$(VERILATOR) $(VFLAGS) -GLANES=$* --top-module tb_dct_engine --Mdir $(BUILD)/engine_L$* \
	    -o tb $(TB)/tb_dct_engine.sv $(ENGINE_RTL) $(REF_C)
	@for s in $(SEEDS); do \
	    ./$(BUILD)/engine_L$*/tb +verilator+seed+$$s +seed=$$s $(RUNFLAGS) || exit 1; \
	done
	./$(BUILD)/engine_L$*/tb +verilator+seed+1 +seed=1 +stall_in=0 +stall_out=0 $(RUNFLAGS)

array: $(foreach N,$(ENGINES_LIST),array_N$(N))

array_N%: $(ARRAY_RTL) $(TB)/tb_dct_array.sv $(REF_C)
	$(VERILATOR) $(VFLAGS) -GNUM_ENGINES=$* --top-module tb_dct_array --Mdir $(BUILD)/array_N$* \
	    -o tb $(TB)/tb_dct_array.sv $(ARRAY_RTL) $(REF_C)
	@for s in $(SEEDS); do \
	    ./$(BUILD)/array_N$*/tb +verilator+seed+$$s +seed=$$s $(RUNFLAGS) || exit 1; \
	done
	./$(BUILD)/array_N$*/tb +verilator+seed+1 +seed=1 +stall_in=0 +stall_out=0 $(RUNFLAGS)

accel: $(foreach N,$(ACCEL_ENGINES_LIST),accel_N$(N))

accel_N%: $(ACCEL_RTL) $(TB)/tb_dct_accel.sv $(REF_C)
	$(VERILATOR) $(VFLAGS) -GNUM_ENGINES=$* --top-module tb_dct_accel --Mdir $(BUILD)/accel_N$* \
	    -o tb $(TB)/tb_dct_accel.sv $(ACCEL_RTL) $(REF_C)
	@for s in $(SEEDS); do \
	    ./$(BUILD)/accel_N$*/tb +verilator+seed+$$s +seed=$$s $(RUNFLAGS) || exit 1; \
	done
	./$(BUILD)/accel_N$*/tb +verilator+seed+1 +seed=1 +stall_in=0 +stall_out=0 $(RUNFLAGS)

# Generic Yosys synthesis as a "does it synthesise" check (not Cyclone V specific;
# the Quartus fit report is authoritative). Full log and cell counts in build/.
synth: $(ACCEL_RTL) synth.ys
	@mkdir -p $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log synth.ys
	@echo "synth: OK (cell counts in $(BUILD)/synth_stat.txt, log in $(BUILD)/synth.log)"

clean:
	rm -rf $(BUILD)

# Top-level Makefile.
#
#   make c                     build + test the C reference natively (logs to docs/logs/)
#   make c IMG=out/x.bmp       ... plus PSNR at Q scales x1/x2/x4/x8 (recon images in out/)
#   make sim                   Verilator lint + RTL tests (logs to docs/logs/sim.md)
#   make lint                  lint only
#   make synth                 Yosys synthesis check of dct_accel
#   make clean

IMG ?=

.PHONY: c sim lint synth clean

c:
	$(MAKE) -C HPS test LOGDIR=$(CURDIR)/docs/logs OUTDIR=$(CURDIR)/out \
	    $(if $(IMG),IMG=$(abspath $(IMG)))

sim:
	$(MAKE) -C FPGA/sim LOGDIR=$(CURDIR)/docs/logs

lint:
	$(MAKE) -C FPGA/sim lint

synth:
	$(MAKE) -C FPGA/sim synth

clean:
	$(MAKE) -C HPS clean
	$(MAKE) -C FPGA/sim clean

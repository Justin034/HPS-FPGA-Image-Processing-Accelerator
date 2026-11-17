# HPS Makefile - C reference library and (from phase 5) the dctq program.
#
#   make                 build build/test_ref
#   make test            run the self-test (logs to LOGDIR)
#   make test IMG=x.bmp  ... plus PSNR at Q scales x1/x2/x4/x8, recon images in OUTDIR
#   make clean
#
# Usually run from the top level: make c [IMG=...]

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS += -Iinclude
LDLIBS   += -lm

BUILD  := build
LOGDIR ?= ../docs/logs
OUTDIR ?= ../out
IMG    ?=

# C reference library (shared by test_ref, dctq and the Verilator DPI-C testbenches)
REF_SRCS := src/dct_ref.c src/test_blocks.c src/image.c src/quality.c \
            src/ref_selftest.c src/reslog.c
REF_OBJS := $(REF_SRCS:src/%.c=$(BUILD)/%.o)
HEADERS  := $(wildcard include/*.h)

.PHONY: all native test clean

all native: $(BUILD)/test_ref

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test_ref: tests/test_ref.c $(REF_OBJS) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ref.c $(REF_OBJS) -o $@ $(LDLIBS)

test: $(BUILD)/test_ref
	mkdir -p $(OUTDIR)
	./$(BUILD)/test_ref --log $(LOGDIR) --tmp $(BUILD) \
	    $(if $(IMG),$(IMG) --recon-prefix $(OUTDIR)/recon)

clean:
	rm -rf $(BUILD)

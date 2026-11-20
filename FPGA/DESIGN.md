# FPGA/ — Design and Reasoning

SystemVerilog for the accelerator, its Verilator testbenches, and (later) the Platform Designer / Quartus files.

- **Phase 2:** `dct_engine`, one DCT + quantizer engine (sections 2–7).
- **Phase 3:** `dct_array`, several engines behind one stream (section 8).
- **Phase 4:** `dct_accel`, registers and data-path feeders around the array (section 10), with `dct_ram` for the option-2 buffers.

---

## 1. Where the engine sits

```
            changes per data-path option                    never changes
 HPS ──► [ feeder: registers / buffer / mSGDMA ] ──► [ dct_array ──► dct_engine × N ] ──► back out
                                                       (phase 3)      (phase 2)
                         Avalon-ST in: uint8 pixels            Avalon-ST out: int16 coefficients
```

The engine only sees streams. It never knows which data-path option is feeding it.

---

## 2. `dct_engine` block diagram

```
                       q_we / q_addr / q_data  (reciprocals R, written once)
                                   │
                                   ▼
                              ┌─────────┐
                              │ r_mem   │ 64 × 17 bit
                              └────┬────┘
 in_data ─► level shift ─► ┌─────────┐   PASS1   ┌─────────┐   PASS2   ┌─────────┐      │
 (8 px/beat) (XOR 0x80)    │ x_mem   │──────────►│ t_mem   │──────────►│ d_mem   │──────┤
                           │ 64 × 8  │  LANES    │ 64 × 16 │  LANES    │ 64 × 16 │      ▼
                           └─────────┘  MACs     └─────────┘  MACs     └─────────┘  quantizer × 4
                                ▲         ▲                     ▲                  (|D|·R+2^15)>>16
                                │      C ROM (Q1.15)         C ROM                     │
                                │                                                      ▼
                           in_ready                                         out_data (4 coef/beat)
                                                                            out_valid / sop / eop
```

### State machine (single buffer)

```
        ┌──────────┐ 8 beats  ┌──────────┐ 64 cycles ┌──────────┐ 64 cycles ┌──────────┐
 reset─►│  LOAD    │─────────►│  PASS1   │──────────►│  PASS2   │──────────►│   OUT    │
        │ in_ready │          │ T = X·Cᵀ │           │ D = C·T  │           │out_valid │
        └──────────┘          └──────────┘           └──────────┘           └────┬─────┘
             ▲                                                       16 beats    │
             └───────────────────────────────────────────────────────────────────┘
```

Cycle counts shown for `LANES = 8`, `DATA_WIDTH = 64`.

---

## 3. How the lanes divide the work

Each pass computes 64 outputs, each a dot product of 8 pairs. Lane `l` handles output `o = grp·LANES + l`, element `(row, col) = (o/8, o%8)`, one product per cycle for `k = 0..7`:

| Pass | Lane computes | Operand A | Operand B |
|---|---|---|---|
| PASS1 | `T[row][col] = Σk X[row][k]·C[col][k]` | `x_mem[row·8+k]` (sign-extended) | ROM `C[col][k]` |
| PASS2 | `D[row][col] = Σk C[row][k]·T[k][col]` | ROM `C[row][k]` | `t_mem[k·8+col]` |

At `k = 7` the lane writes `(acc + product + 2^14) >>> 15` to `t_mem` / `d_mem` and clears its accumulator.

| `LANES` | Cycles per pass | Multipliers (passes) | Cycles per block* |
|---|---|---|---|
| 1 | 512 | 1 | 1048 |
| 2 | 256 | 2 | 536 |
| 4 | 128 | 4 | 280 |
| **8** (default) | **64** | **8** | **152** |
| 16 | 32 | 16 | 88 |

\* `DATA_WIDTH = 64`, no backpressure: 8 load + 2 passes + 16 output beats.

With `LANES = 8`, one group is one full row, which is the simplest way to explain it: "8 multipliers, each produces one column of the row."

---

## 4. Arithmetic and widths

| Signal | Width | Range | Note |
|---|---|---|---|
| `x_mem` | signed 8 | −128..127 | `pixel XOR 0x80` = `pixel − 128` in two's complement |
| C ROM | signed 16 | ±16069 | Q1.15, same 64 integers as `dct_ref.c` |
| product | signed 32 | | 16 × 16 |
| accumulator | signed 32 | ≲ 4.7 × 10⁷ | ample headroom |
| `t_mem` | signed 16 | ≲ ±362 | after round + shift |
| `d_mem` | signed 16 | ≲ ±1024 | after round + shift |
| `r_mem` | unsigned 17 | 257..65536 | `R = round(65536 / Qs)` |
| quantizer product | unsigned 33 | ≲ 2^26 | 16 × 17 |
| output `q` | signed 16 | ≲ ±1024 | |

`>>>` on signed values is a floor shift, the same as `dct_asr()` in the C reference. The C reference is the definition; the testbench checks the engine against it bit for bit.

---

## 5. Interface

| Port | Dir | Width | Meaning |
|---|---|---|---|
| `clk`, `rst` | in | 1 | 50 MHz clock; synchronous active-high reset |
| `q_we`, `q_addr`, `q_data` | in | 1, 6, 17 | Write reciprocal `R` at index `q_addr` |
| `in_valid`, `in_ready`, `in_data` | in, out, in | 1, 1, `DATA_WIDTH` | Avalon-ST sink; `DATA_WIDTH/8` pixels per beat, first pixel in the lowest byte |
| `out_valid`, `out_ready`, `out_data` | out, in, out | 1, 1, `DATA_WIDTH` | Avalon-ST source; `DATA_WIDTH/16` coefficients per beat, first in the lowest 16 bits |
| `out_sop`, `out_eop` | out | 1 | First / last beat of a block |
| `busy` | out | 1 | A block is in progress |

A transfer happens on a rising edge where `valid && ready`. The engine counts beats itself, so input `sop/eop` aren't needed.

---

## 6. Design decisions and reasoning

| Decision | Reasoning | Alternative rejected |
|---|---|---|
| **Matrix form with reusable MAC lanes** | Easy to explain (two matrix multiplies) and to scale (`LANES`). Both passes share the same multipliers. | Fast DCT (Loeffler/AAN): fewer multipliers, much harder to explain and verify |
| **`LANES` parameter (default 8)** | Trades multipliers for speed without changing the design. 8 lanes = one row per group = simple mental model. | Fixed single MAC (the old `matrix_mult_8x8`): ~1000 cycles per block |
| **Single buffer** | Simplest control: load → compute → output. Several engines (phase 3) keep throughput up. | Double buffering: ~10 % faster per engine, more control logic |
| **Level shift on entry (XOR 0x80)** | The HPS sends `uint8`, halving transfer size; the shift costs no adder. `x_mem` only needs 8 bits. | HPS sends `int16` |
| **Transposes by addressing** | `C[col][k]` and `t_mem[k·8+col]` are read directly; nothing is physically transposed. | A transpose step and buffer |
| **ROM as a `case` function** | Portable across Verilator and Quartus (unpacked-array constants are not reliably supported by Quartus Lite). Synthesises to small LUT ROMs. | `localparam` array or `$readmemh` |
| **Quantize on output** | Quantization happens as each beat is sent (4 small multipliers for 64-bit beats), so it costs no extra cycles and no extra buffer. | A separate quantize pass: +64 cycles, or 64 multipliers |
| **Reciprocal table written through a port** | The HPS loads `R` once; changing the Q scale needs no hardware change. Each engine keeps its own copy, written by broadcast, so engines never compete for it. | Constant table: no Q-scale experiments |
| **Round, then arithmetic shift** | Matches `dct_ref.c`: round to nearest, floor shift. | Truncation: bias |
| **Synchronous reset; data arrays not reset** | Control is reset; arrays are always fully written before being read, so resetting them would waste logic. The random-initial-value simulations prove this. | Resetting every array |
| **One-cycle MAC (read, multiply, add)** | Simple, and comfortably inside 20 ns at 50 MHz on Cyclone V DSP blocks. Quartus timing confirms it in phase 6. | Pipelined MAC: more control for no need at 50 MHz |
| **`DATA_WIDTH` parameter (default 64)** | 64 bits = one 8-pixel block row per input beat; matches the planned mSGDMA width. | Fixed width |

---

## 7. Verification (`tb/tb_dct_engine.sv`)

```
 ┌──────────────────── Verilator (C++) ────────────────────┐
 │                                                          │
 │  tb_dct_engine.sv                 dpi_ref.c (DPI-C)      │
 │   ├─ driver ─ dpi_test_pixel ───► test_blocks.c          │
 │   │    random in_valid gaps                              │
 │   ├─ dct_engine (DUT)                                    │
 │   ├─ monitor ─ dpi_ref_q ───────► dct_ref.c              │
 │   │    random out_ready gaps                             │
 │   │    on mismatch: compare dut.t_mem / dut.d_mem         │
 │   │    with dpi_ref_t / dpi_ref_d → names the bad stage  │
 │   └─ result ─ dpi_log_sim ──────► reslog.c → docs/logs/sim.md
 └──────────────────────────────────────────────────────────┘
```

| Feature | Purpose |
|---|---|
| C reference through DPI-C | Same expected values as the board uses; no vector files |
| 79 edge cases + 200 random blocks | Named edge cases are easy to debug; 64 worst-case blocks catch overflow in any coefficient |
| Random gaps on input and output (20 % / 30 %) | Checks the valid/ready handshakes under backpressure |
| Random initial register values, several seeds | Catches registers that are used before reset |
| Stage diagnosis on mismatch | Says whether pass 1, pass 2 or the quantizer is wrong |
| SOP/EOP check | Output framing |
| Extra run without backpressure | Measures cycles per block |
| `LANES = 1` and `8` | The parameter really works, not only the default |
| Drive and sample on the falling edge | No races with the design's rising-edge logic |
| Automatic logging | Each run appends a row to `docs/logs/sim.md` |

Lint: `verilator --lint-only -Wall` on the RTL must be clean.

---

### Results (phase 2)

All runs pass, bit-exact: 279 blocks per run, `LANES = 1` and `8`, seeds 1–3 with backpressure plus a timing run. Measured without backpressure: **152 cycles/block at `LANES = 8`**, 1048 at `LANES = 1`, exactly as calculated. Details in `docs/logs/sim.md`.

---

## 8. `dct_array` (phase 3)

`NUM_ENGINES` copies of `dct_engine` behind the same stream interface, so it is a drop-in replacement for one engine.

```
                         ┌──────────────┐
                    ┌───►│  engine 0    │───┐
 in stream          │    └──────────────┘   │          out stream
 ──────► distributor├───►│  engine 1    │───┤collector ──────►
         (in_sel)   │    └──────────────┘   │(out_sel)
                    └───►│  engine N-1  │───┘
                         └──────────────┘
          q_we / q_addr / q_data ──► broadcast to every engine
```

### How it works

| Part | Behaviour |
|---|---|
| Distributor | `in_sel` points at the engine receiving the current block. `in_valid` goes only to that engine; `in_ready` comes from it. After a whole block (8 beats at 64-bit) `in_sel` moves to the next engine. |
| Collector | `out_sel` points at the engine whose block is next in order. Only that engine's output is passed through. After the beat with `eop`, `out_sel` moves on. |
| Q table | Written to all engines at once. |
| `busy` | Any engine busy, or a block partly received. |

**Why the order is always right:** block *i* goes to engine *i mod N*, and the collector reads engines in the same order. Every block takes exactly the same number of cycles and each engine holds one block at a time, so engine *i mod N* always finishes block *i* before it can accept block *i + N*. No tags or reorder buffer are needed.

### Expected throughput (`LANES = 8`, `DATA_WIDTH = 64`, no backpressure)

Each engine needs 152 cycles per block; N engines work in parallel, but the shared output stream needs 16 beats per block.

| `NUM_ENGINES` | Cycles per block | Limited by |
|---|---|---|
| 1 | 152 | engine |
| 2 | ~76 | engines |
| 4 | ~38 | engines |
| 8 | ~19 | engines |
| 10+ | 16 | output stream (64-bit) |

Above about 9 engines a 64-bit stream is the limit; a wider `DATA_WIDTH` would be needed. On the board, the HPS/DMA transfer rate will probably limit earlier, which is what the engine sweep (phase 10) measures.

### Design decisions

| Decision | Reasoning | Alternative rejected |
|---|---|---|
| **Round-robin, whole blocks** | Simplest possible distributor; order is preserved for free because all blocks take the same time. | First-free with tags + reorder buffer: needed only if block times varied |
| **Same interface as one engine** | `dct_accel` (phase 4) and the testbench treat 1 or N engines identically. | A different interface for the array |
| **Broadcast Q table** | One write reaches every engine; each engine reads its own copy without contention. | One shared table with N read ports |
| **Muxes, not a bus** | `in_ready` / `out_*` are simple selects on a small index; cheap and fast at 50 MHz. | Arbitration logic |

### Verification (`tb/tb_dct_array.sv`)

Same approach as the engine testbench (C reference through DPI-C, random backpressure, random initial values, several seeds, automatic logging), plus:

- Output block *n* must equal input block *n*: this checks the order.
- On a mismatch it reports which engine processed the block and whether the beat matches a neighbouring block instead (which would mean blocks came out of order).
- Runs `NUM_ENGINES` = 1, 2, 4, each with a timing run, to show throughput scaling.

---

## 9. Build and test

```
make lint        # strict lint of dct_engine, dct_array, dct_accel
make sim         # lint + engine (LANES=1, 8) + array (NUM_ENGINES=1, 2, 4) + accel (NUM_ENGINES=1, 2),
                 # seeds 1 2 3 + timing runs
make synth       # Yosys: dct_accel synthesises (generic cells, small buffers)
make -C FPGA/sim engine LANES_LIST=8 SEEDS="4 5 6 7"     # more seeds
make -C FPGA/sim array ENGINES_LIST="8"                  # 8 engines
make -C FPGA/sim accel ACCEL_ENGINES_LIST="4"            # accelerator with 4 engines
make -C FPGA/sim engine SCALE=8                          # another Q scale
```

---

## 10. `dct_accel` (phase 4)

The component that goes into Platform Designer. It wraps `dct_array` with everything the HPS talks to, so the same engines serve all four data-path options.

```
                 ┌──────────────────────────────── dct_accel ──────────────────────────────────┐
  CSR slave ────►│ registers: CTRL STATUS NUM_BLOCKS CYCLE_COUNT INFO BUF_BLOCKS MAGIC SCRATCH  │
  (LW bridge,    │            Q_TABLE ──(broadcast)──► engines                                  │
   32-bit)       │                                                                              │
                 │ MODE 0  IN_BLOCK regs (64×8) ──┐                     ┌──► OUT_BLOCK regs     │
                 │                                │   ┌────────────┐    │     (64×16)           │
  buffer slave ─►│ MODE 1  in_ram  (dct_ram) ─────┼──►│ dct_array  │────┼──► out_ram (dct_ram) ◄─┼── buffer slave
  (main bridge,  │                                │   │ N engines  │    │                       │   (reads)
   64-bit)       │ MODE 2  asi_* (mSGDMA M→S) ────┘   └────────────┘    └──► aso_* (mSGDMA S→M)  │
                 └──────────────────────────────────────────────────────────────────────────────┘
```

### Register map (CSR slave, byte offsets)

| Offset | Name | Access | Meaning |
|---|---|---|---|
| `0x000` | `CTRL` | R/W | bit 0 `START` (write 1; self-clearing) · bit 1 `SOFT_RESET` (write 1; self-clearing) · bits 5:4 `MODE` (0 reg, 1 buffer, 2 stream) |
| `0x004` | `STATUS` | R | bit 0 `BUSY` · bit 1 `DONE` (cleared by the next `START`) · bit 2 `ERROR` · bit 3 `ENGINES_BUSY` (debug) |
| `0x008` | `NUM_BLOCKS` | R/W | Blocks per `START` in modes 1 and 2 (mode 0 always does one) |
| `0x00C` | `CYCLE_COUNT` | R | Cycles from `START` to `DONE` |
| `0x010` | `INFO` | R | `[7:0]` NUM_ENGINES · `[15:8]` LANES · `[27:16]` DATA_WIDTH · `[31:28]` version (1) |
| `0x014` | `BUF_BLOCKS` | R | Capacity of the mode-1 buffer in blocks (1024 by default) |
| `0x018` | `MAGIC` | R | `0x44435431` ("DCT1"): confirms the component is at this address |
| `0x01C` | `SCRATCH` | R/W | No effect; bus write/readback test |
| `0x100–0x1FC` | `Q_TABLE` | R/W | 64 reciprocals `R` (17 bit); read back from a shadow copy |
| `0x200–0x2FC` | `IN_BLOCK` | R/W | Mode 0: 64 pixels, one per word (bits 7:0) |
| `0x300–0x3FC` | `OUT_BLOCK` | R | Mode 0: 64 coefficients, one per word, sign-extended |

**`ERROR`** is set, and nothing starts, when `START` is written with `MODE` = 3, with `NUM_BLOCKS` = 0 (modes 1, 2), with `NUM_BLOCKS` > `BUF_BLOCKS` (mode 1), or while `BUSY` (the running job continues). The next accepted `START` or a `SOFT_RESET` clears it.

**`SOFT_RESET`** acts in the cycle it is written: it stops the job and clears `BUSY`/`DONE`/`ERROR`, the counters and the engines. `MODE`, `NUM_BLOCKS`, the Q table and the buffers are kept. `dctq` uses it after a timeout.

`MODE` and `NUM_BLOCKS` can't be changed while `BUSY`. `MODE` is written together with `START` (`CTRL = START | MODE << 4`).

### Buffer slave (mode 1)

| Word offset | Byte offset (64-bit) | Contents |
|---|---|---|
| `0 … BUF_IN_WORDS−1` | `0x00000 – 0x0FFFF` | Input pixels in block order (HPS writes; can be read back while idle) |
| `BUF_IN_WORDS … +BUF_OUT_WORDS−1` | `0x10000 – 0x2FFFF` | Output coefficients in block order (HPS reads; writes ignored) |

Byte enables are honoured on the input region, so any CPU store width works. The span is 256 KB (18 address bits of bytes, 15 of 64-bit words).

### How each mode moves data

| Mode | Input side | Output side | `DONE` when |
|---|---|---|---|
| 0 | Beat *k* of the block is built from `IN_BLOCK[8k … 8k+7]` | Each beat writes 4 entries of `OUT_BLOCK` | the block's last beat is stored |
| 1 | `in_ram` read port is handed to the feeder; it reads one word ahead whenever a beat is taken, so the RAM output always holds the next beat (one priming cycle per job) | Each beat is written to `out_ram` at the next address | `NUM_BLOCKS` blocks stored |
| 2 | `asi_ready` = array ready **and** a job is running **and** blocks remain: no beat is taken before `START` or beyond `NUM_BLOCKS` | `aso_*` follows the array; `startofpacket` on the job's first beat, `endofpacket` on its last | `NUM_BLOCKS` blocks accepted by the sink |

In modes 0 and 1 the output side is always ready, so the engines never stall.

### Expected cycles (`LANES = 8`, no backpressure)

| | `NUM_ENGINES = 1` | `NUM_ENGINES = 2` |
|---|---|---|
| Mode 0, per `START` | ≈ 154 (152 + priming + finish) | ≈ 154 (one block uses one engine) |
| Mode 1 / 2, per block | ≈ 152 | ≈ 76 |

The HPS-side cost of mode 0 (128 bridge accesses per block) will dominate on the board; the hardware cycles are for reference.

### Design decisions

| Decision | Reasoning | Alternative rejected |
|---|---|---|
| **One component, `MODE` register** | One Quartus build serves options 1, 2 and 3/4; the board switches by software. Every option uses the same engines, so comparisons are fair. | A separate build per option |
| **Two Avalon-MM slaves** | Registers on the lightweight bridge (small, slow, fine for control); the buffer on the main bridge (wide, fast). Matches `idea.md` §4. | Buffer behind the lightweight bridge: 32-bit, slow |
| **Separate input and output RAMs, each simple dual-port** | One write and one read port per RAM is the form Quartus infers most reliably as M10K (with byte enables). The HPS writes `in_ram` and reads `out_ram`; the engines read `in_ram` and write `out_ram`. | One true dual-port RAM: two write ports, less portable inference |
| **Input-buffer read port shared (Avalon when idle, feeder when running)** | Keeps `in_ram` simple dual-port. Reading the input back is only needed for checks, when idle. | A third port |
| **Read-ahead address instead of a FIFO** | The feeder presents `addr + 1` to the RAM when a beat is taken, so data is ready the next cycle with no skid buffer. | A small FIFO between RAM and array |
| **`IN_BLOCK`/`OUT_BLOCK` as registers** | 64 bytes in, 128 bytes out: tiny, and mode 0 needs 8 pixels in parallel per beat. | RAM: would need a wide parallel read |
| **Q table shadow** | Engines' tables are write-only; the shadow lets `hwcheck` read the table back. | No readback |
| **`ERROR` instead of silently ignoring bad commands** | A wrong `NUM_BLOCKS` or a double `START` shows up on the board at once instead of as a timeout. | Ignore silently |
| **Mode 2 accepts exactly `NUM_BLOCKS` blocks** | If the mSGDMA sends extra data, or starts before `START`, nothing enters the engines out of step. | Free-running stream |
| **`SOFT_RESET` in the write cycle** | The next `STATUS` read already shows idle; no wait needed in `dctq`. | A registered pulse: one cycle late |
| **`MAGIC` and `SCRATCH`** | `hwcheck` can prove it is talking to this component at the expected address, and test the bus, without side effects. | Only `INFO` |

### Verification (`tb/tb_dct_accel.sv`)

The testbench drives the slaves like the HPS (`csr_write`, `csr_read`, `buf_write`, `buf_read`, polling `STATUS`) and the stream ports like the two mSGDMAs.

| Step | Checks |
|---|---|
| Registers | Reset values, `INFO`, `MAGIC`, `BUF_BLOCKS`, `SCRATCH` and all 64 `Q_TABLE` entries read back |
| Errors | `MODE` 3, `NUM_BLOCKS` 0, `NUM_BLOCKS` > `BUF_BLOCKS` → `ERROR` only; `SOFT_RESET` clears it |
| Mode 0 | All 279 test blocks through `IN_BLOCK`/`OUT_BLOCK`, one `START` each; `IN_BLOCK` readback |
| Mode 1 | All blocks in one `START`; every third word written in two halves plus one write with no byte enabled; input readback; cycles per block from `CYCLE_COUNT` |
| Busy / reset | `START` while busy → `BUSY|ERROR`; `SOFT_RESET` mid-job → idle, Q table kept; re-run with different blocks so stale output can't pass |
| Mode 2 | All blocks through the streams with random gaps on both sides; SOP/EOP on first/last beat of the job; no beat accepted before `START` or after the job |

Runs for `NUM_ENGINES` = 1 and 2, seeds 1–3 plus a run without stream backpressure; one row per run in `docs/logs/sim.md` (cycles/block column = mode 1).

Yosys (`make synth`) synthesises `dct_accel` with small buffers as a portability check; Quartus is authoritative.

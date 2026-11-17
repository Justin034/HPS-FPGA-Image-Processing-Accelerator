# FPGA/ — Design and Reasoning

SystemVerilog for the accelerator, its Verilator testbenches, and (later) the Platform Designer / Quartus files.

- **Phase 2:** `dct_engine`, one DCT + quantizer engine (sections 2–7).
- **Phase 3:** `dct_array`, several engines behind one stream (section 8).
- Phase 4 adds `dct_accel` (registers and data-path feeders).

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
make lint        # strict lint of dct_engine and dct_array
make sim         # lint + engine (LANES=1, 8) + array (NUM_ENGINES=1, 2, 4), seeds 1 2 3 + timing runs
make -C FPGA/sim engine LANES_LIST=8 SEEDS="4 5 6 7"     # more seeds
make -C FPGA/sim array ENGINES_LIST="8"                  # 8 engines
make -C FPGA/sim engine SCALE=8                          # another Q scale
```

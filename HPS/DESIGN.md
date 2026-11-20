# HPS/ — Design and Reasoning

C code that runs on the HPS (ARM Cortex-A9, Linux) and, for testing, natively on the Linux PC.

- **Phase 1 (sections 1–7):** the C reference library: arithmetic, test blocks, image handling, reconstruction/PSNR, self-test and automatic result logging.
- **Phase 5 (section 8):** the `dctq` program and the FPGA hardware layer.

---

## 1. One reference, three users

```
                     ┌──────────────────────── C reference library ────────────────────────┐
                     │ dct_ref.c      bit-exact DCT + quantizer (same maths as the FPGA)   │
                     │ test_blocks.c  79 edge cases + seeded random blocks                  │
                     │ image.c        BMP I/O, RGB→gray, pad, block order                   │
                     │ quality.c      dequantize + inverse DCT, MSE, PSNR                   │
                     │ ref_selftest.c checks against floating point                          │
                     │ reslog.c       automatic result tables (markdown + CSV)              │
                     └──────┬──────────────────────────┬──────────────────────────┬────────┘
                            │                          │                          │
               Linux PC: make c             Verilator testbench          Board: dctq (phase 5)
               test_ref → self-test,        (phase 2) calls dct_ref       software baseline,
               PSNR per Q scale,            and test_blocks through       checks FPGA output,
               logs to docs/logs/           DPI-C                         PSNR, timing → results/
```

**Why a single reference:** the software baseline, the board's correctness check and the simulation all use the same code, so "the FPGA matches the reference" means the same thing everywhere. There's no second model that can drift out of step.

**Risk and mitigation:** a bug in the reference would be copied into everything it checks. `ref_selftest.c` therefore checks it against an independent floating-point DCT and against exact division, and reconstruction PSNR must be plausible. (An earlier Python model with identical arithmetic passed the same checks.)

---

## 2. Data flow through the library

```
 photo.bmp (24-bit, from scripts/png_to_bmp.py)
   │ bmp_load_rgb                                    image.c
   ▼
 RGB image ── preprocess_run ──────────────────────────────────────────────┐
   │   gray:  Y = (77R + 150G + 29B) >> 8                                   │ timed as
   │   pad:   up to multiples of 8, edge rows/columns replicated            │ "preprocess"
   │   order: blocks one after another (64 bytes each)                      │
   ▼                                                                        ┘
 uint8 blocks ── dct_ref_blocks (software)  or  FPGA (phase 5) ──► int16 q blocks
   │                                                                  │
   │                                       reconstruct_blocks         │ quality.c
   │                                       (q·Qs → float IDCT → +128 → clamp)
   ▼                                                                  ▼
 grayscale image  ◄────────── gray_mse / psnr_from_mse ───────── reconstructed image
                                         │
                                         ▼
                              reslog_append → quality.md / .csv
```

---

## 3. The arithmetic (`dct_ref.c`)

```
 pixel (uint8, 0..255)
   │  X = pixel − 128
   ▼
 X  (−128..127)
   │  pass 1:  T[r][u] = ( Σk X[r][k]·C[u][k] + 2^14 ) >> 15        T = X·Cᵀ   (rows)
   ▼
 T  (|T| ≲ 362)
   │  pass 2:  D[v][u] = ( Σk C[v][k]·T[k][u] + 2^14 ) >> 15        D = C·T    (columns)
   ▼
 D  (|D| ≲ 1024)                              DCT coefficients
   │  q = sign(D) · ( (|D|·R + 2^15) >> 16 )      R = round(65536 / Qs)
   ▼
 q  (int16)                                   output
```

- `C` = `DCT_Q15`, the 8×8 DCT-II matrix in Q1.15. Largest entry 16069.
- `Qs = clamp(JPEG_LUMA × scale, 1, 255)`; `R` ≤ 65536 (17 bits).
- `>>` is a floor shift. `dct_asr()` implements it explicitly, because `>>` on negative values is implementation-defined in C. It matches SystemVerilog `>>>` on signed values.
- Accumulators: at most about 8 × 362 × 16069 ≈ 4.7 × 10⁷, well inside 32 bits (the C code uses 64-bit for safety; the hardware uses 32-bit).
- The self-test prints the actual maximum `|T|` and `|D|`.

---

## 4. Design decisions and reasoning

| Decision | Reasoning | Alternative rejected |
|---|---|---|
| **Single C reference** | One definition of "correct" for software baseline, board check and simulation. | Separate Python model + C code + testbench model: three references that can drift |
| **Matrix constants written out** | The RTL ROM copies the same 64 integers. The self-test checks them against the cosine formula. | Computing at run time in each language: rounding differences |
| **Q1.15 constants** | Largest constant 0.49, so Q1.15 gives the most precision that still fits a 16-bit multiplier input. | Q2.14 (old `dct_quantizer.sv`): loses a bit for no benefit |
| **Round before every shift** | Plain `>> 15` rounds toward −∞, biasing results down by up to 1 over two passes. Adding `2^14` first gives round-to-nearest for one adder. | Truncation: measurable bias, especially on DC |
| **Explicit floor shift (`dct_asr`)** | C leaves `>>` on negative numbers implementation-defined. Making it explicit guarantees PC, ARM and simulation agree. | Relying on GCC's behaviour |
| **Same two-pass structure as the hardware** | Pass-1 and pass-2 values can be compared one-to-one with the RTL when debugging (`dct_ref_block` returns them). | Fast DCT in software: faster, but a different algorithm from the hardware |
| **Level shift in the reference (and the engine)** | The HPS sends `uint8` pixels, halving transfer size. | Sending level-shifted `int16` |
| **Reciprocal quantizer** | One multiply + shift instead of 64 dividers in hardware; the HPS computes `R`, so changing the Q scale needs no hardware change. | Hardware dividers |
| **Round half away from zero in the quantizer** | Symmetric: −2.5 → −3 and 2.5 → 3, so negative coefficients aren't biased. | Round half up on signed values |
| **Accept ±1 vs exact division** | `1/Qs` can't always be represented exactly. The self-test counts the cases; tests stay bit-exact because the reference uses the same formula. | Wider `R` or a correction step |
| **Integer grayscale `(77R + 150G + 29B) >> 8`** | Identical everywhere, fast on the ARM. Weights sum to 256, so white stays 255. | Floating-point weights |
| **Edge-replicating padding** | Constant padding creates an artificial edge in border blocks. | Zero padding |
| **Block order in memory** | A block isn't contiguous in a row-by-row image; block order lets a DMA read one contiguous range. | Strided DMA |
| **Preprocessing split into alloc + run** | Allocation happens once, outside the timed region; `preprocess_run` is what gets timed. | Allocating inside the timed loop |
| **Floating-point inverse DCT for reconstruction** | The decoder isn't measured, so the most accurate inverse makes PSNR reflect only the FPGA's loss. | Fixed-point inverse |
| **Test blocks from a per-block seed (xorshift32, integers only)** | Any block can be generated on its own, in any order, identically on PC, ARM and inside Verilator. | A shared random sequence (order-dependent), or floating-point noise (platform-dependent) |
| **79 edge-case blocks first** | Named cases are easy to debug; 64 worst-case blocks (one per coefficient) catch overflow in any position. | Random blocks only |
| **Automatic logging (`reslog.c`)** | Every test/run appends a row (date, host, configuration) to a markdown table and a CSV; no copying results by hand. | Hand-recorded results |
| **Markdown + CSV** | Markdown renders on GitHub and reads as plain text; CSV opens in a spreadsheet. | One format only |
| **Logging never stops a program** | A log write failure prints a warning; the measurement still runs. | Aborting on a log error |
| **C11, standard library + POSIX only** | Builds unchanged with the PC's gcc and the board's gcc; no extra libraries. | Third-party libraries |

---

## 5. Modules

| File | Contents |
|---|---|
| `include/dct_ref.h`, `src/dct_ref.c` | `JPEG_LUMA`, `DCT_Q15`, `qtable_scaled`, `qtable_reciprocals`, `dct_asr`, `dct_quantize`, `dct_ref_block` (optionally returns pass-1/pass-2 values), `dct_ref_blocks` (the software baseline) |
| `include/test_blocks.h`, `src/test_blocks.c` | `test_block(index, px)`, `test_block_name(index)`, `test_block_count(n_random)`, `TEST_SAMPLE_BLOCK` |
| `include/image.h`, `src/image.c` | `bmp_load_rgb`, `bmp_save_gray`, `gray_from_rgb`, `gray_pad8`, `gray_crop`, `gray_to_blocks`, `blocks_to_gray`, `preprocess_alloc` / `preprocess_run` |
| `include/quality.h`, `src/quality.c` | `dct_float_matrix`, `dct_float_block`, `idct_block`, `reconstruct_blocks`, `gray_mse`, `psnr_from_mse` |
| `include/ref_selftest.h`, `src/ref_selftest.c` | `ref_selftest()`: 13 checks, PASS/FAIL per check, summary for the log |
| `include/reslog.h`, `src/reslog.c` | `reslog_set_dir`, `reslog_append`, `reslog_timestamp`, `reslog_host`; tables `RESLOG_TESTS`, `RESLOG_RUNS`, `RESLOG_QUALITY` |
| `tests/test_ref.c` | PC test program: self-test + optional image PSNR at ×1/×2/×4/×8 |
| `src/main.c` | `dctq`: commands `selftest`, `hwcheck`, `blocktest`, `run` (phase 5, section 8) |
| `include/fpga.h`, `src/fpga.c` | Hardware layer through `/dev/mem` on the board; stubs on the PC (phase 5) |
| `include/accel_regs.h` | `dct_accel` register map and default addresses (mirrors `FPGA/DESIGN.md` §10) |
| `include/timing.h`, `src/timing.c` | `timing_now` (`CLOCK_MONOTONIC`), `timing_median` |

### Test blocks

| Index | Content |
|---|---|
| 0–14 | all 0, all 255, all 128, all 127, two checkerboards, horizontal / vertical / diagonal ramps, vertical / horizontal stripes, positive and negative impulses, count 0..63, JPEG example block |
| 15–78 | worst case for coefficient (v, u): pixel 255 where `C[v][y]·C[u][x] > 0`, else 0 |
| 79+ | random: even = uniform noise, odd = smooth (base + gradient + small noise) |

### Result tables (`reslog.c`)

| Table | Columns |
|---|---|
| `tests` | Date, Host, Test, Mode, Config, Result, Details |
| `runs` | Date, Host, Image, Size, Mode, Engines, Lanes, Scale, Runs, Preprocess ms, Transfer ms, Compute ms, Total ms, SW total ms, Speedup, Cycles, Match, PSNR dB |
| `quality` | Date, Host, Image, Size, Mode, Scale, MSE, PSNR dB |

---

## 6. Self-test (`ref_selftest.c`)

| # | Check | Why |
|---|---|---|
| 1 | `DCT_Q15` equals the cosine formula | Catches typos in the hand-written table |
| 2 | Matrix is orthogonal within rounding | Confirms the constants form a valid DCT |
| 3 | Reciprocals fit 17 bits; ×1/×2/×4/×8 tables valid | Sizes the `Q_TABLE` registers |
| 4 | All-128 block → all zeros | Level shift is correct |
| 5 | Constant-255 block → DC ≈ 1016, AC = 0 | Scaling end to end |
| 6 | Fixed point within 2.5 of float (79 edge + 2000 random blocks) | Precision is adequate |
| 7 | Max `|T|`, `|D|` fit int16 (incl. worst cases) | Sizes the hardware storage |
| 8 | Quantized output within 1 of float quantization, all scales | Overall accuracy |
| 9 | Reciprocal vs exact division: differences ≤ 1, count reported | Documents the accepted limitation |
| 10 | Block order round trip | `gray_to_blocks` / `blocks_to_gray` are inverses |
| 11 | Padding 13×10 → 16×16 replicates edges | Matches the padding rule |
| 12 | Smooth 64×64 image: PSNR > 30 dB at ×1 | Encode + reconstruct work together |
| 13 | BMP save/load round trip (odd width) | BMP row padding and orientation |

---

## 7. Build and test (phase 1)

```
make c                                  # self-test; row appended to docs/logs/tests.md
make c IMG=out/<image>_256x256.bmp      # + PSNR at x1/x2/x4/x8 → docs/logs/quality.md,
                                        #   reconstructed images out/recon_<image>_s<scale>.bmp
```
Flags: `-O2 -Wall -Wextra -std=c11`, linked with `-lm`. The same sources build on the board in phase 5.

---

## 8. `dctq` (phase 5)

One command-line program for the board. It uses the reference library from sections 1–7 for the software path and for checking, and `fpga.c` for the hardware.

```
                          dctq (main.c)
   ┌─────────────────────────────────────────────────────────────────────┐
   │  selftest ── ref_selftest.c                                         │
   │  hwcheck ─── fpga.c: MAGIC, INFO, SCRATCH, Q_TABLE, SOFT_RESET,     │
   │              ERROR, buffer, 1 block mode reg, 4 blocks mode buf     │
   │  blocktest ─ test_blocks.c ──► sw / FPGA ──► compare with dct_ref.c │
   │  run IMG ─── image.c (preprocess) ──► sw / FPGA ──► compare          │
   │              ──► quality.c (reconstruct, PSNR) ──► result line       │
   │                                                                     │
   │  every command ──► reslog.c ──► results/{tests,runs,quality}.md/.csv │
   └─────────────────────────────────────────────────────────────────────┘
                 │ fpga.c (board)                 │ fpga.c (PC: stubs)
                 ▼                                ▼
     /dev/mem ── registers 0xFF210000      "hardware not available";
              └─ buffer    0xC4000000       only --mode sw works
```

### Commands

| Command | Does | Logs to |
|---|---|---|
| `selftest` | The 13 reference checks (section 6), now on the ARM | `tests` |
| `hwcheck` | Nine accelerator checks (below), plus buffer copy speed | `tests` |
| `blocktest --mode M` | All test blocks (79 edge + `--nrand` random, default 200) through mode M; every coefficient compared with `dct_ref_block` | `tests` |
| `run IMG --mode M` | Timed runs on an image; match against software; PSNR; optional `--recon` BMP | `runs`, `quality` |

Options: `--mode sw|reg|buf|dma`, `--scale 1|2|4|8`, `--runs N` (default 10), `--recon F.bmp`, `--nrand N`, `--log DIR` (default `results`), `--no-log`, `--tmp DIR`, `--csr ADDR`, `--buf ADDR`. Exit code 0 = pass, 1 = fail, 2 = usage error.

### Modes

| `--mode` | Path | Accelerator `MODE` | How one image is processed |
|---|---|---|---|
| `sw` | ARM, `dct_ref_blocks` | — | One call for all blocks |
| `reg` | Option 1 | 0 | Per block: 64 writes to `IN_BLOCK`, `START`, poll, 64 reads from `OUT_BLOCK` |
| `buf` | Option 2 | 1 | Per chunk of up to `BUF_BLOCKS` blocks: copy in, `START`, poll, copy out |
| `dma` | Options 3/4 | 2 | Not built yet: prints a message (written in phase 9 with the mSGDMA) |

### What `run` measures

```
 one run:  preprocess ──► transfer in ──► compute ──► transfer out
           (image.c)      (copy / regs)   START..DONE  (copy / regs)
```

| Column | `sw` | `reg` | `buf` |
|---|---|---|---|
| Preprocess | BMP data → gray → pad → block order | same | same |
| Transfer | 0 | whole loop minus compute | copy-in + copy-out time |
| Compute | `dct_ref_blocks` | sum over blocks of `START` → `DONE` seen | sum over chunks of `START` → `DONE` seen |
| Total | preprocess + compute | wall time of the whole run | wall time of the whole run |
| Cycles | — | sum of `CYCLE_COUNT` | sum of `CYCLE_COUNT` |

- **Compute is measured the same way in every FPGA mode:** wall time from writing `START` to seeing `DONE`. It includes polling overhead. `Cycles` is the pure hardware time (÷ 50 MHz), so the difference shows the cost of polling over the bridge.
- **Every run also times the software path**, so `SW total` and `Speedup` (= SW total ÷ total) come from the same program, image and moment.
- **Match:** the FPGA's coefficients for the whole image are compared with the software's. `YES`, or `NO (k of n blocks)`.
- **PSNR** is computed from the coefficients the selected mode produced, so a wrong FPGA result also shows as a PSNR change.
- **Timing method:** one warm-up run, then `--runs` timed runs; each column is the median over the runs. The median ignores runs disturbed by other Linux activity.

### `hwcheck`

| # | Check | Pass when |
|---|---|---|
| 1 | `MAGIC` | reads `0x44435431`. If not, stops here with a hint (FPGA not programmed, bridges off, or wrong address) |
| 2 | `INFO` | version 1, ≥ 1 engine, ≥ 1 lane, data width 64; printed |
| 3 | `SCRATCH` | 6 patterns + 32 walking ones read back |
| 4 | `Q_TABLE` | a 17-bit pattern and then the real ×1 table read back (128 reads) |
| 5 | `SOFT_RESET` | `STATUS` = 0 afterwards |
| 6 | `ERROR` | `START` with `NUM_BLOCKS` = 0 in mode 1 gives `ERROR` only; `SOFT_RESET` clears it |
| 7 | Buffer | the whole input region written and read back; copy speed in MB/s printed and logged |
| 8 | Mode `reg` | the JPEG example block matches the reference |
| 9 | Mode `buf` | 4 random blocks match the reference |

A dead bridge can freeze the board on the first read; software can't catch that. The `devmem` LED test (`idea.md` §10, stage 1) comes first for that reason.

### Hardware layer (`fpga.c`)

| Function | Does |
|---|---|
| `fpga_open` / `fpga_close` | Open `/dev/mem` with `O_SYNC` (uncached), map 1 KB of registers and the 256 KB buffer window; read `INFO`, `BUF_BLOCKS` |
| `fpga_rd` / `fpga_wr` | One 32-bit register access |
| `fpga_buf_write` / `fpga_buf_read` | Copy to/from the buffer with 32-bit accesses |
| `fpga_soft_reset`, `fpga_load_qtable`, `fpga_start` | As named |
| `fpga_wait` | Poll `STATUS` until `BUSY` clears; after 1 s: `SOFT_RESET`, return timeout |
| `fpga_reg_block` | One whole mode-0 block, returning its `START`→`DONE` time and `CYCLE_COUNT` |

### Design decisions

| Decision | Reasoning | Alternative rejected |
|---|---|---|
| **One program, four commands** | One binary to copy and build on the board; the commands are the bring-up order. | Separate test programs |
| **Hardware layer and stubs in one `fpga.c`, chosen by `__arm__`** | The same file list builds on the PC and the board with one plain `gcc` command, which matters if the board has no `make`. On the PC, `--mode sw` and the tests run; hardware modes fail cleanly. | A separate stub file: the board and PC builds would need different file lists |
| **Default addresses `0xFF210000` / `0xC4000000`, overridable** | Platform Designer is set to match (phase 6). `--csr`/`--buf` handle a different assignment without recompiling. | Hard-coded addresses only; reading them from a device tree (more setup) |
| **`_FILE_OFFSET_BITS 64` for `mmap`** | Addresses above 2 GB (`0xC4000000`, `0xFF210000`) would be negative in a 32-bit `off_t`. | Relying on 32-bit wrap-around |
| **32-bit copy loops instead of `memcpy`** | `memcpy` may use unaligned or multi-register accesses, which uncached device memory on ARM may reject (bus error). 32-bit accesses always work; the bridge's byte enables handle the 64-bit slave. Phase 8 can try wider accesses if the copy speed limits option 2. | `memcpy`: possibly faster, risk of `SIGBUS` |
| **Output coefficients read straight into `int16_t` arrays** | The accelerator packs 4 `int16` per 64-bit word, first in the lowest bits; on the little-endian ARM that is exactly an `int16_t` array. No unpacking. | Unpacking word by word |
| **Accelerator checked before every hardware command** | `blocktest` and `run` read `MAGIC` and `INFO` and do a `SOFT_RESET` first, so a wrong address or a left-over job is reported at once. | Trusting the state |
| **Software path in every `run`** | Speedup and match come from the same process and image; no second command needed. | Separate `sw` and FPGA rows matched by hand |
| **Compute = `START`→`DONE` wall time in every FPGA mode, plus cycles** | Comparable across modes; cycles show the pure hardware share. | Cycles only: hides polling overhead |
| **`buf` splits large images into chunks** | Images bigger than the buffer (`BUF_BLOCKS`, 1024 blocks = 256×256) still run, so `buf` can be compared on every image. | Refusing images that don't fit |
| **`dma` deferred to phase 9** | Needs the mSGDMA and reserved memory, which don't exist yet; code written now couldn't be tested. | Untested driver code now |
| **Default log folder `results/`** | On the board next to `dctq`, copied into `docs/board/` afterwards. On the PC, pass `--log docs/logs`. Ignored by git. | Logging into `docs/` on the board |

### Build

```
make -C HPS native        # PC: build/test_ref + build/dctq (stubs)
make -C HPS board         # board, if make exists
# board without make (in HPS/):
mkdir -p build && gcc -O2 -Wall -Wextra -std=c11 -Iinclude \
    src/main.c src/fpga.c src/timing.c src/dct_ref.c src/test_blocks.c \
    src/image.c src/quality.c src/ref_selftest.c src/reslog.c -o build/dctq -lm
```

The file list is explicit, not `src/*.c`, because `src/` still contains the old `data_setup.c`, which is not part of `dctq`.

### Results (phase 5, PC)

All PC checks passed on the first build (2026-11-20, `docs/logs/`):

| Check | Result |
|---|---|
| `dctq selftest` | 13/13 |
| `dctq blocktest --mode sw` | 279 blocks, 0 bad |
| `dctq run test_256x256.bmp --mode sw --runs 10` | PSNR 28.37 dB, MSE 94.616: identical to the phase 1 `test_ref` result |
| `dctq blocktest --mode reg` on the PC | Stops cleanly: hardware not available |

PC timing for reference only (x86, not the board): preprocess 0.166 ms, DCT + quantization 1.936 ms, total 2.131 ms for 1024 blocks (about 1.9 µs per block). The board's Cortex-A9 will be several times slower; its numbers come from `dctq run` in phase 7.

Not yet tested: everything in `fpga.c`'s hardware version, which can only run on the board.

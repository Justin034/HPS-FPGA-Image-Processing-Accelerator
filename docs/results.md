# Results — HPS/FPGA Image Accelerator

Hand-filled project record. Fill in each section as the matching phase in `planning.md` is completed.

---

## 1. Summary

*(Fill in at the end: headline speedup, best configuration, one or two sentences on what limited it.)*

| Best configuration | Image | Software total | FPGA total | Speedup | PSNR |
|---|---|---|---|---|---|
| | | | | | |

---

## 2. Setup

| Item | Value |
|---|---|
| Board | Terasic DE1-SoC (Cyclone V 5CSEMA5F31C6) |
| HPS | Cortex-A9, dual core, ___ MHz |
| Linux image | Intel FPGA University Program DE1-SoC, version ___ |
| Quartus | 21.1 Lite |
| FPGA clock | 50 MHz |
| Compiler (board) | gcc ___, flags `-O2 -Wall -Wextra -std=c11` |
| Git commit used for results | ___ |

---

## 3. Board checks

| Stage | Check | Result | Notes |
|---|---|---|---|
| 0 | `gcc --version` | | |
| 0 | `make --version` | | |
| 0 | Root access | | |
| 0 | File transfer method | | |
| 0 | Disk space | | |
| 0 | `clock_getres` resolution | | |
| 1 | LED test via lightweight bridge | | |
| 1 | `devmem` available | | |
| 1 | Accelerator register read/write | | |
| 1 | Survives JTAG reprogramming | | |
| 2 | Boot arguments location | | |
| 2 | `mem=960M` applied (`free -m`) | | |
| 2 | Reserved buffer read/write | | |
| 3 | FPGA-to-SDRAM ports out of reset | | |
| 3 | mSGDMA test transfer | | |

---

## 4. Simulation results (Linux)

| Test | Simulator | Vectors | Result | Notes |
|---|---|---|---|---|
| `golden.py --selftest` | Python | | | |
| Engine, `LANES=1` | Verilator | | | |
| Engine, `LANES=8` | Verilator | | | |
| Engine, `LANES=8` | Icarus | | | |
| Array, `NUM_ENGINES=1/2/4` | Verilator | | | |
| Accelerator, `MODE` 0/1/2 | Verilator | | | |
| Yosys synthesis | Yosys | — | | |
| HPS C native tests | gcc | | | |

Simulated cycles per block (one engine, `LANES=8`): ___

---

## 5. FPGA build results (Quartus)

| `NUM_ENGINES` | ALMs (%) | DSP blocks (%) | M10K (%) | Fmax (MHz) | Meets 50 MHz? | Compile time |
|---|---|---|---|---|---|---|
| 1 | | | | | | |
| 2 | | | | | | |
| 4 | | | | | | |
| 8 | | | | | | |

Platform Designer addresses:

| Slave | Bridge | Base address |
|---|---|---|
| `dct_accel` registers | Lightweight | |
| `dct_accel` buffer | Main HPS-to-FPGA | |
| mSGDMA in CSR / descriptor | Lightweight | |
| mSGDMA out CSR / descriptor | Lightweight | |

---

## 6. Correctness on hardware

| Mode | Image | Self-test | Bit-exact vs `golden.py` | PSNR (dB) |
|---|---|---|---|---|
| sw | | — | | |
| reg (option 1) | | | | |
| buf (option 2) | | | | |
| dma (option 3) | | | | |
| dma (option 4) | | | | |

---

## 7. Timing results

Median of 10 runs after 1 warm-up. All times in milliseconds. Speedup = software total ÷ configuration total.

### 7.1 256×256 (all options)

| Configuration | Engines | Preprocess | Transfer | Compute | Total | `CYCLE_COUNT` | Speedup |
|---|---|---|---|---|---|---|---|
| Software | — | | — | | | — | 1× |
| Option 1 (reg) | 1 | | | | | | |
| Option 2 (buf) | 1 | | | | | | |
| Option 3 (dma) | 1 | | | | | | |
| Option 4 (dma) | 1 | | | | | | |

### 7.2 640×480

| Configuration | Engines | Preprocess | Transfer | Compute | Total | `CYCLE_COUNT` | Speedup |
|---|---|---|---|---|---|---|---|
| Software | — | | — | | | — | 1× |
| Option 1 (reg) | 1 | | | | | | |
| Option 3 (dma) | | | | | | | |
| Option 4 (dma) | | | | | | | |

### 7.3 1920×1080

| Configuration | Engines | Preprocess | Transfer | Compute | Total | `CYCLE_COUNT` | Speedup |
|---|---|---|---|---|---|---|---|
| Software | — | | — | | | — | 1× |
| Option 3 (dma) | | | | | | | |
| Option 4 (dma) | | | | | | | |

### 7.4 Engine sweep

Image: ___ · Data path: ___

| Engines | Compute (ms) | Total (ms) | `CYCLE_COUNT` | Speedup vs software | Speedup vs 1 engine |
|---|---|---|---|---|---|
| 1 | | | | | |
| 2 | | | | | |
| 4 | | | | | |
| 8 | | | | | |

---

## 8. Quality vs quantization scale

Image: ___ (PSNR against the grayscale image)

| Q scale | MSE | PSNR (dB) | SSIM | Run time unchanged? |
|---|---|---|---|---|
| ×1 | | | | |
| ×2 | | | | |
| ×4 | | | | |
| ×8 | | | | |

---

## 9. Observations

- Where the time goes (transfer vs compute) for each option:
- Point where adding engines stopped helping, and why:
- Anything surprising:

---

## 10. Known limitations

- The reciprocal-multiply quantizer can differ from exact division by ±1 in rare cases (`golden.py` uses the same formula, so results are bit-exact with the model).
- Grayscale only; colour not processed.
- Software baseline uses the same matrix algorithm as the FPGA, not a fast DCT.
- *(add others found during testing)*

---

## 11. Issues log

| Date | Phase | Problem | Fix |
|---|---|---|---|
| | | | |

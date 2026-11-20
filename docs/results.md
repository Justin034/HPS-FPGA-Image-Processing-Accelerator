# Results — HPS/FPGA Image Accelerator

The numbers are recorded **automatically** by the tools. This document holds only what a person has to write: setup, summary, observations, limitations and issues.

## Generated result tables

| Table | Written by | Location |
|---|---|---|
| PC tests (C reference self-test) | `make c` (`test_ref`) | [logs/tests.md](logs/tests.md) · `logs/tests.csv` |
| PSNR per Q scale (PC) | `make c IMG=...` | [logs/quality.md](logs/quality.md) · `logs/quality.csv` |
| Simulation | `make sim` | [logs/sim.md](logs/sim.md) |
| Quartus resources and timing | `FPGA/quartus/report_summary.tcl` | [logs/quartus.md](logs/quartus.md) |
| Board tests (selftest, hwcheck, blocktest) | `dctq` | [board/tests.md](board/tests.md) · `board/tests.csv` |
| Board timing, speedup, match, PSNR | `dctq run` | [board/runs.md](board/runs.md) · `board/runs.csv` |
| Board PSNR per Q scale | `dctq run` | [board/quality.md](board/quality.md) · `board/quality.csv` |

After each board session, copy the board's `results/` folder contents into `docs/board/`.

---

## 1. Summary

*(Fill in at the end: headline speedup, best configuration, what limited it. Take the numbers from `board/runs.md`.)*

| Best configuration | Image | Software total (ms) | FPGA total (ms) | Speedup | PSNR (dB) |
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

## 3. Board checks (stage 0, by hand)

Shell checks done once on the board; later stages are covered by `dctq hwcheck` and logged automatically.

| Check | Result | Notes |
|---|---|---|
| `gcc --version` | | |
| `make --version` | | |
| `whoami` (root) | | |
| File transfer method | | |
| `df -h` | | |
| `cat /proc/cmdline` (before `mem=`) | | |
| `free -m` (after `mem=960M`) | | |

---

## 4. Observations

- Where the time goes (transfer vs compute) for each option:
- Point where adding engines stopped helping, and why:
- Effect of Q scale on PSNR (and confirmation that run time doesn't change):
- Anything surprising:

---

## 5. Known limitations

- The reciprocal-multiply quantizer can differ from exact division by ±1 in rare cases (the C reference uses the same formula, so results are bit-exact with it).
- Grayscale only; colour not processed.
- The software baseline uses the same matrix algorithm as the FPGA, not a fast DCT.
- Option 2's buffer (64 KB in + 128 KB out, 1024 blocks) holds at most a 256×256 image. Whether it fits next to the DE1-SoC Computer's own memories is confirmed only by the Quartus fit (phase 6).
- In option 2, the input buffer can't be read back while a job is running (its read port belongs to the engines then).
- *(add others found during testing)*

---

## 6. Issues log

| Date | Phase | Problem | Fix |
|---|---|---|---|
| 2026-11-20 | 4 (`dct_accel` sim) | All 8 first `dct_accel` runs FAIL with `2 errors`, no bad blocks, ~3,500 cycles/block in mode 2: the job never reached `DONE`. Testbench bug: the mode-2 output monitor dropped `aso_ready` before the rising edge of the last transfer, so the accelerator never saw its last beat accepted. RTL was correct. | Monitor holds `aso_ready` one more cycle after the last beat. All runs PASS. Details in `FPGA/DESIGN.md` §10. The FAIL rows remain in `logs/sim.md` as a record. |

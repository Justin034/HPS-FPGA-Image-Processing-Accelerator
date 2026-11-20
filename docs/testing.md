# Testing Sequence

The complete order of commands, from the Linux PC through Windows to the board, ending with the results in the repository.

- Run commands from the **repository root** unless stated otherwise.
- **Status** column: ✅ = works now · 🔜 Pn = added in phase n (this document is updated when that phase is written).
- Every test logs its result automatically (`docs/logs/` on the PC, `results/` on the board). Nothing needs to be copied by hand except the final `results/` folder.
- If any step fails, stop there: the later steps depend on it.

---

## Stage A — Linux PC (development checks)

Run before every commit and before going to Windows.

| # | Command | Checks | Pass when | Logs to | Status |
|---|---|---|---|---|---|
| A1 | `python3 scripts/png_to_bmp.py test_images/<img>.png --resize 256x256` | Test image for the board | `out/<img>_256x256.bmp` written; "fits option 2 buffer: yes" | — | ✅ |
| A2 | `python3 scripts/png_to_bmp.py test_images/<img>.png --resize 640x480` | Larger images (options 1, 3, 4) | BMP written | — | ✅ |
| A3 | `python3 scripts/png_to_bmp.py test_images/<img>.png --resize 1920x1080` | Large-image case (options 3, 4) | BMP written | — | ✅ |
| A4 | `make c` | C reference self-test | `SELFTEST PASSED: 13/13` | `docs/logs/tests.md` | ✅ |
| A5 | `make c IMG=out/<img>_256x256.bmp` | Encode + reconstruct, PSNR ×1/×2/×4/×8 | PSNR falls with scale; `out/recon_*.bmp` look right | `docs/logs/quality.md` | ✅ |
| A6 | `make lint` | Strict Verilator lint of all RTL (`dct_engine`, `dct_array`, `dct_accel`) | `lint: OK`, no warnings | — | ✅ |
| A7 | `make sim` | Engine (`LANES` 1, 8), array (`NUM_ENGINES` 1, 2, 4), accelerator modes 0/1/2 (`NUM_ENGINES` 1, 2), seeds 1–3 + timing runs | Every run `PASS`; array ~152 / 76 / 38 cycles/block; accel ~152 / 76 | `docs/logs/sim.md` | ✅ |
| A8 | `make synth` | Yosys: `dct_accel` synthesises (generic cells, small buffers) | `synth: OK`; cell counts in `FPGA/sim/build/synth_stat.txt` | — | ✅ |
| A9 | `make -C HPS native` then `./HPS/build/dctq selftest` | `dctq` builds natively | `SELFTEST PASSED` | `docs/logs/tests.md` | 🔜 P5 |
| A10 | `./HPS/build/dctq blocktest --mode sw` | Software path on all test blocks | `PASS` | `docs/logs/tests.md` | 🔜 P5 |
| A11 | `./HPS/build/dctq run out/<img>_256x256.bmp --mode sw --runs 10 --recon out/recon_sw.bmp` | Full software run with timing + PSNR | Result line printed; PSNR matches A5 at ×1 | `docs/logs/runs.md` | 🔜 P5 |

Optional extra coverage:
```
make -C FPGA/sim engine LANES_LIST=8 SEEDS="4 5 6 7"
make -C FPGA/sim array ENGINES_LIST="8"
make -C FPGA/sim accel ACCEL_ENGINES_LIST="4" SEEDS="4 5"
make -C FPGA/sim engine SCALE=8
```

Then commit:
```
git add -A && git commit -m "<message>"
```

---

## Stage B — Windows (Quartus build)

🔜 P6. Exact file and project names are filled in when phase 6 is written.

| # | Step | Command / action | Pass when |
|---|---|---|---|
| B1 | Get the repository onto Windows | `git pull` (or copy the folder) | Same commit as stage A |
| B2 | Create the Quartus project | `quartus_sh -t FPGA/quartus/create_project.tcl` | Project created, RTL files listed |
| B3 | Add the accelerator to the DE1-SoC Computer | Platform Designer: add `dct_accel` (from `FPGA/ip/dct_accel_hw.tcl`); registers → lightweight bridge, buffer → main HPS-to-FPGA bridge; Generate HDL | Generation without errors; note the base addresses |
| B4 | Compile | `quartus_sh --flow compile <project>` | No errors; timing met at 50 MHz |
| B5 | Record resources and timing | Runs automatically after compile (`FPGA/quartus/report_summary.tcl`) | Row added to `docs/logs/quartus.md` |
| B6 | First compile only | Copy the DE1-SoC Computer `.qsys`, `.qsf`, top-level Verilog into `FPGA/system/` | Files committed |
| B7 | Program the FPGA | `quartus_pgm -m jtag -o "p;output_files/<project>.sof@2"` (device 2 on the DE1-SoC JTAG chain; device 1 is the HPS) | "Configuration succeeded" |

Repeat B3–B7 for each configuration: `NUM_ENGINES` = 1 first; 2, 4, 8 for the engine sweep (stage E); mSGDMA additions for options 3 and 4.

---

## Stage C — Board setup (once)

Run in a shell on the board (serial console or SSH). Record results in `docs/results.md` §3 (these are the only checks recorded by hand).

| # | Command | Checks | Pass when |
|---|---|---|---|
| C1 | `gcc --version` and `make --version` | Compiler on the board | Both print a version (else cross-compile, see `idea.md` §10) |
| C2 | `whoami` | Root for `/dev/mem` | `root` (or use `sudo`) |
| C3 | `df -h` | Space for images | A few hundred MB free |
| C4 | From the PC: `scp -r HPS out/*.bmp root@<board-ip>:~/dct/` | File transfer | Files arrive (else SD card / USB) |
| C5 | `cd ~/dct/HPS && make board` | Build `dctq` on the board | `build/dctq` created, no warnings — 🔜 P5 |

---

## Stage D — Board tests per data-path option

After programming the FPGA (B7), on the board in `~/dct/HPS`. Every command appends to `results/`.

### D1 — Hardware link and software baseline (every new FPGA build)
| # | Command | Checks | Pass when | Status |
|---|---|---|---|---|
| D1.1 | `./build/dctq selftest` | C reference on the ARM | `SELFTEST PASSED` | 🔜 P5 |
| D1.2 | `./build/dctq hwcheck` | Bridges, LEDs, register read/write, `INFO`, `Q_TABLE` readback | `PASS`; `INFO` shows the expected `NUM_ENGINES` | 🔜 P5 |
| D1.3 | `./build/dctq run ../<img>_256x256.bmp --mode sw --runs 10` | Software baseline timing on the board | Result line; PSNR as on the PC | 🔜 P5 |

### D2 — Option 1: register mode (phase 7)
```
./build/dctq blocktest --mode reg
./build/dctq run ../<img>_256x256.bmp --mode reg --runs 10 --recon recon_reg.bmp
./build/dctq run ../<img>_640x480.bmp  --mode reg --runs 10
```
Pass when: `blocktest` PASS; `run` shows `match_sw YES`.

### D3 — Option 2: internal buffer (phase 8, completes core scope)
```
./build/dctq blocktest --mode buf
./build/dctq run ../<img>_256x256.bmp --mode buf --runs 10 --recon recon_buf.bmp
for s in 1 2 4 8; do ./build/dctq run ../<img>_256x256.bmp --mode buf --scale $s --runs 10; done
```
Pass when: PASS, `match_sw YES`; PSNR falls with scale, run time stays the same.

### D4 — Option 3: mSGDMA via FPGA-to-HPS bridge (phase 9, target)
Before: add `mem=960M` to the kernel boot arguments, reboot, then check:
```
cat /proc/cmdline        # contains mem=960M
free -m                  # about 960 MB total
```
Then:
```
./build/dctq hwcheck                 # also tests the reserved buffer
./build/dctq blocktest --mode dma
for img in ../<img>_256x256.bmp ../<img>_640x480.bmp ../<img>_1920x1080.bmp; do
    ./build/dctq run $img --mode sw  --runs 10
    ./build/dctq run $img --mode dma --runs 10
done
```

### D5 — Option 4: mSGDMA via FPGA-to-SDRAM ports (phase 11, stretch)
FPGA design loaded at boot (see `idea.md` §10, stage 3). Same commands as D4. If `hwcheck` reports the ports in reset or the bus hangs, stop: option 4 is dropped.

---

## Stage E — Engine sweep (phase 10, target)

For each `NUM_ENGINES` in 1, 2, 4, 8 (as many as fit):
1. Windows: set `NUM_ENGINES`, regenerate, compile, program (B3–B7). Resources are logged to `docs/logs/quartus.md`.
2. Board:
   ```
   ./build/dctq hwcheck                      # confirms NUM_ENGINES via INFO
   ./build/dctq blocktest --mode dma         # or --mode buf if option 3 isn't done
   ./build/dctq run ../<img>_640x480.bmp   --mode dma --runs 10
   ./build/dctq run ../<img>_1920x1080.bmp --mode dma --runs 10
   ```
The `runs` table records the engine count from `INFO` automatically.

---

## Stage F — Collect results

From the PC, after each board session:
```
scp -r root@<board-ip>:~/dct/HPS/results/* docs/board/
scp root@<board-ip>:~/dct/HPS/recon_*.bmp out/           # optional: view reconstructions
git add docs/ && git commit -m "Board results <date>"
```
Then write the summary, observations and limitations in `docs/results.md` using the tables in `docs/board/` and `docs/logs/`.

---

## Quick reference

| Where | Minimum check before moving on |
|---|---|
| PC | `make c` · `make lint` · `make sim` all pass |
| Windows | Compile with timing met; `quartus.md` row added |
| Board | `selftest` → `hwcheck` → `blocktest --mode M` → `run IMG --mode M` |
| Afterwards | `results/` copied to `docs/board/`, committed |

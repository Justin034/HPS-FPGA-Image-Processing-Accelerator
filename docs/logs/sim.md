# Simulation results (Verilator)

| Date | Host | Test | Config | Seed | Blocks | Cycles/block | Result | Details |
|---|---|---|---|---|---|---|---|---|
| 2026-11-17 00:37:47 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 1056.6 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:37:48 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 1056.8 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:37:48 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 1056.3 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:37:48 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 1048.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:38:20 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 160.8 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:38:20 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 161.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:38:20 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 160.4 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:38:20 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 152.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:52 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 1056.6 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:53 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 1056.8 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:53 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 1056.3 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:53 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 1048.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:53 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 160.8 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:53 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 161.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:53 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 160.4 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:48:53 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 152.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-17 00:49:11 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 160.7 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:49:11 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 160.9 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:49:11 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 160.4 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:49:11 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 152.0 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:49:39 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 80.6 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:49:39 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 80.6 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:49:39 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 80.5 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:49:39 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 76.3 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:50:11 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 40.5 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:50:11 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 40.7 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:50:11 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 40.6 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-17 00:50:11 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 38.2 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 00:59:05 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 1056.6 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:06 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 1056.8 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:06 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 1056.3 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:06 | nil | dct_engine | dct_engine LANES=1 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 1048.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:37 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 160.8 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:37 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 161.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:37 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 160.4 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:37 | nil | dct_engine | dct_engine LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 152.0 | PASS | all 17856 coefficients bit-exact |
| 2026-11-20 00:59:55 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 160.7 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 00:59:55 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 160.9 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 00:59:55 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 160.4 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 00:59:55 | nil | dct_array | dct_array NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 152.0 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:00:26 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 80.6 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:00:26 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 80.6 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:00:26 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 80.5 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:00:26 | nil | dct_array | dct_array NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 76.3 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:01:01 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 40.5 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:01:01 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 40.7 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:01:01 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 40.6 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:01:01 | nil | dct_array | dct_array NUM_ENGINES=4 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 38.2 | PASS | all 17856 coefficients bit-exact, in order |
| 2026-11-20 01:01:51 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 152.0 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3527.9 cyc/blk) |
| 2026-11-20 01:01:51 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 152.0 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3527.6 cyc/blk) |
| 2026-11-20 01:01:52 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 152.0 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3527.8 cyc/blk) |
| 2026-11-20 01:01:53 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 152.0 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3519.2 cyc/blk) |
| 2026-11-20 01:02:41 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 76.3 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3448.0 cyc/blk) |
| 2026-11-20 01:02:42 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 76.3 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3448.2 cyc/blk) |
| 2026-11-20 01:02:43 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 76.3 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3447.8 cyc/blk) |
| 2026-11-20 01:02:43 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 76.3 | FAIL | 2 errors (bad blocks: reg 0, buf 0, stream 0); reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (3443.4 cyc/blk) |
| 2026-11-20 01:05:22 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 152.0 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (160.7 cyc/blk) |
| 2026-11-20 01:05:22 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 152.0 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (160.5 cyc/blk) |
| 2026-11-20 01:05:22 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 152.0 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (160.6 cyc/blk) |
| 2026-11-20 01:05:22 | nil | dct_accel | dct_accel NUM_ENGINES=1 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 152.0 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (152.0 cyc/blk) |
| 2026-11-20 01:06:11 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 1 | 279 | 76.3 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (80.9 cyc/blk) |
| 2026-11-20 01:06:11 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 2 | 279 | 76.3 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (81.0 cyc/blk) |
| 2026-11-20 01:06:12 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=20/30 | 3 | 279 | 76.3 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (80.6 cyc/blk) |
| 2026-11-20 01:06:12 | nil | dct_accel | dct_accel NUM_ENGINES=2 LANES=8 DATA_WIDTH=64 scale=1 stall=0/0 | 1 | 279 | 76.3 | PASS | reg 279 blk (153 cyc/START), buf 279 blk, stream 279 blk (76.3 cyc/blk) |

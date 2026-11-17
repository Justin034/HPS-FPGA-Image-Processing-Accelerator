# scripts/ — Design and Reasoning

This folder holds the project's only Python: `png_to_bmp.py`. It runs **on the PC only**, by hand, before images are copied to the board.

Everything else that used to be planned in Python (reference model, test vectors, decoder, PSNR, timing analysis) now lives in C on the board. See `HPS/DESIGN.md`.

---

## 1. Where it fits

```
            PC                                         DE1-SoC board
 ┌──────────────────────────────┐            ┌──────────────────────────────────────┐
 │ test_images/photo.png        │            │ dctq run photo_256x256.bmp --mode M  │
 │        │                     │            │   read BMP → gray → pad → blocks      │
 │        ▼                     │   copy     │   FPGA (or software) DCT + quantize  │
 │ png_to_bmp.py ──► out/*.bmp ─┼───────────►│   check vs C reference, PSNR, timing │
 │   (optional resize)          │ scp / SD   │   prints one result line              │
 └──────────────────────────────┘            └──────────────────────────────────────┘
```

---

## 2. What it does

```
input image (PNG, JPEG, …)
   │  Pillow: convert to RGB (drops alpha / palette)
   ▼
optional resize (Lanczos) to e.g. 256x256
   │
   ▼
24-bit uncompressed BMP  ──►  out/<name>.bmp  or  out/<name>_<W>x<H>.bmp
   │
   └─► prints size, padded size, block count, and whether it fits option 2's buffer
```

Example:
```
python3 scripts/png_to_bmp.py test_images/photo.png --resize 256x256
test_images/photo.png -> out/photo_256x256.bmp
    256x256, padded 256x256, 1024 blocks, fits option 2 buffer: yes
```

---

## 3. Design decisions and reasoning

| Decision | Reasoning | Alternative rejected |
|---|---|---|
| **Convert PNG → BMP on the PC** | The board has no Python and no PNG library. BMP is trivial to parse in C, so the board code stays dependency-free. | Decoding PNG on the board (`stb_image.h`): a new third-party dependency for work that isn't being measured |
| **24-bit uncompressed BMP** | One fixed format means `image.c` only handles one case. Pillow writes this by default for RGB images. | Supporting palette / 32-bit / compressed BMPs: more C code, no benefit |
| **Keep colour in the BMP; grayscale on the board** | The HPS's preprocessing (RGB → gray, padding, block ordering) is part of the timed pipeline, so it must happen on the board. | Converting to gray on the PC: removes work that should be measured |
| **Optional resize** | Makes standard sizes (256×256 for option 2, 640×480, 1920×1080) from any photo. Lanczos keeps the image sharp. | Requiring images of exact sizes |
| **Size suffix in the file name when resized** | Several sizes of the same photo can sit side by side in `out/`. | Overwriting the same name |
| **Report padded size, block count, option 2 fit** | Shows straight away whether an image can be used with option 2 (buffer sized for 256×256). | Finding out on the board |
| **Output to `out/`** | `out/` is git-ignored, so generated images never end up in commits. | Writing next to the source image |
| **No grayscale reference file** | The board computes PSNR against the grayscale image it already holds in memory. | A separate reference file to copy and keep in sync |

---

## 4. Usage

```
python3 scripts/png_to_bmp.py IMAGE [IMAGE ...] [--out-dir out] [--resize WxH]
```

Requirements: Python 3.8+ and Pillow. Run from the repository root.

# Changelog

## v2.0.0 — 2026-10-04

Complete rewrite.  
The whole public API has been renamed and the chip layer has been split from the shared drawing core (`glcd.c`, `glcd.h`) that is also used by `avr-ks0108` and `avr-ssd1306`.  
Portfolio project, no backward-compatibility shims.

### Renames

| v1 | v2 |
|---|---|
| `GLCD_Setup()` | `pcd8544_init()` |
| `GLCD_Render()` | `glcd_render()` (chip-layer impl) |
| `GLCD_Clear()` / `GLCD_ClearLine()` / `GLCD_FillScreen()` | `glcd_clear()` / `glcd_clear_line()` / `glcd_fill_screen()` |
| `GLCD_GotoXY()` / `GotoLine()` / `GetX/Y/Line()` | `glcd_goto()` / `goto_line()` / `get_x/y/line()` |
| `GLCD_SetPixel()` / `SetPixels()` | `glcd_set_pixel()` / `glcd_fill_rect()` (duplicate removed) |
| `GLCD_Draw*()` family | `glcd_draw_line/rect/round_rect/triangle/circle/bitmap()` (shared glcd core) |
| `GLCD_Fill*()` family | `glcd_fill_rect/round_rect/triangle/circle()` |
| `GLCD_InvertMode()` | `glcd_set_inverted(bool)` / `glcd_is_inverted()` |
| `GLCD_InvertRect()` | `glcd_invert_rect()` |
| `GLCD_SetFont(name, w, h, mode)` | `glcd_set_font(&font_tahoma_11x13, GLCD_MERGE)` |
| `GLCD_GetWidthChar/String/String_P()` | `glcd_get_char_width/get_string_width/get_string_width_p()` |
| `GLCD_PrintChar/String/String_P/Integer/Double()` | `glcd_print_char/string/string_p/int/float()` |
| `GLCD_SendCommand` / `GLCD_Data` | internal `static send_command()` / `send_data()` |
| hard-coded Vop | `pcd8544_set_contrast(uint8_t vop)` |
| `GLCD_White/Black/Overwrite/Merge` | `GLCD_WHITE/BLACK/OVERWRITE/MERGE` |
| `Tahoma11x13` et al as `uint8_t[]` | `font_tahoma_11x13` as `const glcd_font` with PROGMEM data |
| `GLCD_SCE` / `GLCD_RST` / `GLCD_DC` / `GLCD_DIN` / `GLCD_SCLK` | `PCD8544_PIN_SCE/RST/DC/DIN/SCLK` |

### Fixes — PCD8544 chip layer

1. The v1 demo drew a 126x62 rounded rectangle on an 84x48 screen,  
   so nothing rendered inside the panel bounds.  
   The new demo draws within the 84x48 viewport.
2. The v1 header's description called the module "KS0108" and documented a KS0108 driver;  
   corrected to PCD8544.
3. `GLCD_Render` overwrote the text cursor as a side effect of walking the panel.  
   `glcd_render()` saves and restores the glcd cursor around the SCE-low burst.
4. The v1 init sequence included three redundant display-control writes ("all segments on", "blank", "normal") during startup.  
   The new init goes straight from reset into normal mode.

### Fixes — shared GLCD core

Inherited from the shared `glcd.c` factored out during the KS0108 rewrite:

1. `SetPixel` outside the screen used to clobber the cursor and silently write a pixel at the previous cursor instead.  
   `glcd_set_pixel()` now guards against `x >= width || y >= height` directly and never touches the cursor.
2. The trailing blank column after a character or bitmap was unconditionally written at `x = width`,  
   which spilled into the next line (and past the end of the buffer on the last line).  
   Fixed by only writing when `x + 1 < width`.
3. `GLCD_GotoY` checked the current `Y` instead of the new one,  
   so `GotoXY(0, 200)` from a valid cursor used to accept the garbage value.  
   `glcd_goto()` clamps against the argument.
4. `GLCD_InvertScreen` was declared in the header but never implemented,  
   so calling it caused a link error.  
   Implemented `glcd_set_inverted()` properly.
5. `FillTriangle` divided by `(y2 - y1)` and used `double`.  
   When two vertices shared a Y the result was a divide-by-zero (undefined behavior, usually a hard fault).  
   Replaced with an integer scanline fill that handles horizontal edges.
6. `SetPixels` and `FillRectangle` with `x1 > x2` or `y1 > y2` underflowed into huge loops that corrupted the whole buffer.  
   Fixed by ordered min/max.
7. `DrawCircle` and `FillCircle` refused to draw when `cx < radius` or `cy < radius`.  
   Replaced with a Bresenham mid-point algorithm that clips each candidate pixel through `glcd_set_pixel()`.
8. `PrintDouble` dropped leading-zero fractions (`0.05 -> "0.5"`) and truncated instead of rounding (`0.3 -> "0.2"`).  
   Replaced with integer scaling, round-half-to-even, and zero-padded decimals.
9. `PrintInteger(INT32_MIN)` printed nothing because the custom `Int2bcd` guarded against the one value whose absolute value doesn't fit in `int32_t`.  
   Replaced with `ltoa` which handles it correctly.

### Performance

- `glcd_render()` sets the X/Y address once and holds SCE low across all 504 bytes,  
  relying on the controller's horizontal auto-increment addressing.  
  The v1 render pulsed SCE for every byte.
- Optional `PCD8544_USE_SPI` config path drives DIN/SCLK from the AVR's hardware SPI peripheral (fast path on ATmega328P using MOSI/PB3 and SCK/PB5).  
  Bit-bang remains the default so the pin map stays flexible.
- `glcd_set_pixel()` is a single buffer-index + bit-mask operation instead of a GotoXY-wrapped read-modify-write.
- `glcd_fill_rect()` and `glcd_invert_rect()` share one `apply_rect()` routine that writes 8-pixel column masks instead of per-pixel pokes.
- `glcd_fill_triangle()` is an integer scanline fill; no libm pulled in.

### Project changes

- Repo renamed `AVR-PCD8544` -> `avr-pcd8544`.
- Restructured to `src/ examples/ tests/ sim/ docs/ scripts/`.
- Added `Build.ps1`, `Simulate.ps1`, `scripts/Common.psm1` for Windows + Linux build/test automation.
- Added host-side Unity tests under `tests/` with fake AVR registers and a bus-op capture seam (`src/pcd8544_internal.h`).
- Added GitHub Actions CI running the matrix build + tests on Ubuntu.
- Fonts (`font_5x8`, `font_tahoma_11x13`, `font_tekton_pro_ext_27x28`) repackaged into `glcd_font` structs with PROGMEM data;  
  glyph bytes are byte-identical to their original exports and shared across all three display repos.
- Datasheet moved to `docs/datasheets/pcd8544.pdf`; demo screenshot to `docs/images/demonstration.png`.
- Added bit-bang `F_CPU` guard: `#error` when `F_CPU > 20 MHz` without `PCD8544_USE_SPI`,  
  since bit-banging would exceed the datasheet's 4 MHz SCLK ceiling.
- New `pcd8544_set_contrast(uint8_t vop)` public call replaces the hard-coded Vop in v1.

## v1 — initial release

Original Arduino-style driver (`GLCD_Setup`, `GLCD_PrintChar`, `Tahoma11x13`, …).

# sim/

SimulIDE circuit for the PCD8544 (Nokia 5110) demo.

## Setup (one-time, Windows)

1. Run `.\Build.ps1 -Mcu atmega328p -OutDir build/sim` from the repo root.
2. Open SimulIDE and build the circuit:
   - MCU: ATmega328P at 8 MHz
   - Nokia 5110 (PCD8544) 84x48 GLCD part
   - Wire per the table in the repo README (RST=PB0, DC=PB1, SCE=PB2, DIN=PB3, SCLK=PB5)
   - VDD/VLCD to +3.3 V, backlight (LED) through a series resistor to +3.3 V
3. Load the firmware: right-click the MCU, *Load firmware*, `build/sim/demo.hex`.
4. Save the circuit as `demo.sim1` here.

After that, `.\Simulate.ps1` builds and opens the circuit automatically.

## Expected behavior

- Top line: `avr-pcd8544 v2` in the 5x8 font.
- Shape sampler: rectangle outline, circle outline, triangle outline arranged across the lower rows.
- Full-screen border.
- The whole display inverts every second via `glcd_set_inverted()`.

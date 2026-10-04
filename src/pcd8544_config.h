#ifndef PCD8544_CONFIG_H
#define PCD8544_CONFIG_H

// User-overridable settings for the PCD8544 chip layer. Every macro has a
// safe default; projects override by -D on the compiler command line or by
// -include'ing an application config header before this file.

#ifndef PCD8544_WIDTH
#define PCD8544_WIDTH 84
#endif
#ifndef PCD8544_HEIGHT
#define PCD8544_HEIGHT 48
#endif

// Pin assignments. Defaults chosen so hardware SPI works on the same wiring
// if enabled (MOSI/SCK = PB3/PB5 on ATmega328P).
#ifndef PCD8544_PIN_RST
#define PCD8544_PIN_RST B, 0
#endif
#ifndef PCD8544_PIN_DC
#define PCD8544_PIN_DC B, 1
#endif
#ifndef PCD8544_PIN_SCE
#define PCD8544_PIN_SCE B, 2
#endif
#ifndef PCD8544_PIN_DIN
#define PCD8544_PIN_DIN B, 3
#endif
#ifndef PCD8544_PIN_SCLK
#define PCD8544_PIN_SCLK B, 5
#endif

// Use hardware SPI if DIN/SCLK are on the hardware SPI pins
// (MOSI/SCK = PB3/PB5 on ATmega328P).
#ifndef PCD8544_USE_SPI
#define PCD8544_USE_SPI 0
#endif

// Default Vop (contrast). 0..127 maps to the Vop command byte with bit 7 set.
// 60 is a typical mid-point for 3.3 V Nokia 5110 breakouts.
#ifndef PCD8544_DEFAULT_CONTRAST
#define PCD8544_DEFAULT_CONTRAST 60u
#endif

#endif

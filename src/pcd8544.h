#ifndef PCD8544_H
#define PCD8544_H

#ifdef __cplusplus
extern "C" {
#endif

#include "glcd.h"
#include "pcd8544_config.h"
#include <stdint.h>

// Initialize the PCD8544 (Nokia 5110) panel: configure pins, pulse reset,
// run the init command sequence, apply the default contrast, and attach the
// shared framebuffer to the glcd core. Call once at startup before any glcd_*
// drawing calls.
void pcd8544_init(void);

// Push the shared framebuffer to the panel in a single SCE-low burst using
// the controller's horizontal auto-increment addressing. Respects
// glcd_set_inverted() and preserves the glcd cursor.
void glcd_render(void);

// Set contrast via the Vop register (bits 0..6; bit 7 is the command flag).
// Values above 127 are clamped to 127.
void pcd8544_set_contrast(uint8_t vop);

#ifdef __cplusplus
}
#endif

#endif

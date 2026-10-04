#ifndef PCD8544_INTERNAL_H
#define PCD8544_INTERNAL_H

// Internal API for pcd8544. Not for application code — subject to change
// without notice. Included only by src/pcd8544.c and tests/test_pcd8544.c.

#include <stdint.h>

// Begin capturing every byte sent over the pcd8544 bus into the
// caller-provided buffer. Each entry is encoded as two bytes:
//   byte 0: flags  (bit 7 = 1 for data, 0 for command)
//   byte 1: value  (the byte written on DIN)
// Pass buffer=NULL to stop capture. capacity is the max number of entries
// (so the physical buffer must be capacity*2 bytes). The library writes at
// most capacity entries and silently drops the rest.
void pcd8544_test_capture_begin(uint8_t *buffer, uint16_t capacity);

// Number of entries captured so far.
uint16_t pcd8544_test_capture_end(void);

#endif

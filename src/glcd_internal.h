#ifndef GLCD_INTERNAL_H
#define GLCD_INTERNAL_H

// Internal API for glcd. Not for application code — subject to change without
// notice. Included only by src/glcd.c and tests/test_glcd.c.

#include <stdint.h>

uint8_t *glcd_test_get_buffer(void);
uint8_t  glcd_test_get_width(void);
uint8_t  glcd_test_get_height(void);

#endif

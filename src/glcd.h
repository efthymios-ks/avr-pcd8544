#ifndef GLCD_H
#define GLCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <avr/pgmspace.h>

// Pixel color. The buffer stores "set" as 1-bit, so GLCD_BLACK means pixel on
// (ink) and GLCD_WHITE means pixel off (background), regardless of panel
// electrical polarity.
typedef enum { GLCD_WHITE = 0, GLCD_BLACK = 1 } glcd_color;

// Text and bitmap blending mode. GLCD_OVERWRITE clears the background to
// white before drawing the glyph. GLCD_MERGE OR-blends the glyph into the
// existing buffer contents.
typedef enum { GLCD_OVERWRITE = 0, GLCD_MERGE = 1 } glcd_text_mode;

// Font descriptor. The glyph bytes live in PROGMEM in the exact packed
// layout produced by the mikroC X-GLCD exporter: one width byte followed by
// width_max * ceil(height / 8) column bytes per glyph. The outer "line"
// (8-row page) stripes are stored column-major with the lines index as the
// inner stride.
typedef struct {
    const uint8_t *data;    // Packed glyph bytes in PROGMEM.
    uint8_t        width;   // Maximum character cell width in pixels.
    uint8_t        height;  // Character cell height in pixels.
    uint8_t        first;   // ASCII code of the first glyph (usually 0x20).
    uint8_t        count;   // Number of glyphs stored.
} glcd_font;

// Attach the chip-specific framebuffer. The buffer must be
// width_px * height_px / 8 bytes and persists for the lifetime of the
// program. Called once by the chip layer during init.
void glcd_attach_buffer(uint8_t *buffer, uint8_t width_px, uint8_t height_px);

void glcd_clear(void);
void glcd_clear_line(uint8_t row);
void glcd_fill_screen(glcd_color color);

void glcd_goto(uint8_t x, uint8_t y);
void glcd_goto_line(uint8_t row);
uint8_t glcd_get_x(void);
uint8_t glcd_get_y(void);
uint8_t glcd_get_line(void);

void glcd_set_pixel(uint8_t x, uint8_t y, glcd_color color);
void glcd_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, glcd_color color);
void glcd_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, glcd_color color);
void glcd_draw_round_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t radius, glcd_color color);
void glcd_draw_triangle(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, glcd_color color);
void glcd_draw_circle(uint8_t cx, uint8_t cy, uint8_t radius, glcd_color color);
void glcd_draw_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bitmap_pgm, glcd_color color);

void glcd_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, glcd_color color);
void glcd_fill_round_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t radius, glcd_color color);
void glcd_fill_triangle(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, glcd_color color);
void glcd_fill_circle(uint8_t cx, uint8_t cy, uint8_t radius, glcd_color color);

// Flip the "inverted" mode flag. In inverted mode glcd_render() pushes the
// bitwise complement of the buffer to the panel; the buffer itself is left
// untouched so toggling in and out is free.
void glcd_set_inverted(bool inverted);
bool glcd_is_inverted(void);

// Invert the pixels inside the rectangle in-place.
void glcd_invert_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);

void glcd_set_font(const glcd_font *font, glcd_text_mode mode);
uint8_t glcd_get_char_width(char c);
uint8_t glcd_get_string_width(const char *s);
uint8_t glcd_get_string_width_p(const char *s_pgm);
void glcd_print_char(char c);
void glcd_print_string(const char *s);
void glcd_print_string_p(const char *s_pgm);
void glcd_print_int(int32_t value);
void glcd_print_float(float value, uint8_t decimals);

#ifdef __cplusplus
}
#endif

#endif

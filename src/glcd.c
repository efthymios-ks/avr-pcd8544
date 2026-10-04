#include "glcd.h"
#include "glcd_internal.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <avr/pgmspace.h>

// Shared GLCD drawing core. This file is the canonical copy; the sibling
// avr-pcd8544 and avr-ssd1306 repos receive byte-identical copies of
// glcd.c and glcd.h.

#define GLCD_PAGE_HEIGHT 8u

// Framebuffer state. The chip-specific layer registers its buffer and the
// panel dimensions via glcd_attach_buffer().
static uint8_t *g_buffer;
static uint8_t  g_width;
static uint8_t  g_height;
static uint8_t  g_pages;      // Height / 8, cached for the inner loops.
static uint8_t  g_cursor_x;
static uint8_t  g_cursor_y;
static bool     g_inverted;

static const glcd_font *g_font;
static glcd_text_mode   g_font_mode;

// Mask ops shared by fill/clear/invert.
typedef enum { MASK_SET = 0, MASK_CLEAR = 1, MASK_INVERT = 2 } mask_op;

static inline uint8_t min_u8(uint8_t a, uint8_t b) { return a < b ? a : b; }
static inline uint8_t max_u8(uint8_t a, uint8_t b) { return a > b ? a : b; }
static inline int16_t abs_i16(int16_t v) { return v < 0 ? -v : v; }

// Low-level buffer access. Each byte stores an 8-pixel vertical strip (LSB is
// the top pixel), matching the KS0108/SSD1306/PCD8544 page layout.
static void apply_byte_mask(uint8_t x, uint8_t page, uint8_t mask, mask_op op)
{
    uint16_t idx = (uint16_t)page * g_width + x;
    switch (op) {
    case MASK_SET:    g_buffer[idx] |= mask;  break;
    case MASK_CLEAR:  g_buffer[idx] &= (uint8_t)~mask; break;
    case MASK_INVERT: g_buffer[idx] ^= mask; break;
    }
}

static void apply_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, mask_op op)
{
    if (!g_buffer || w == 0 || h == 0) return;
    if (x >= g_width || y >= g_height) return;
    if ((uint16_t)x + w > g_width)  w = (uint8_t)(g_width - x);
    if ((uint16_t)y + h > g_height) h = (uint8_t)(g_height - y);

    uint8_t y_end = (uint8_t)(y + h - 1);
    uint8_t first_page = (uint8_t)(y / GLCD_PAGE_HEIGHT);
    uint8_t last_page = (uint8_t)(y_end / GLCD_PAGE_HEIGHT);
    uint8_t top_bits = (uint8_t)(y % GLCD_PAGE_HEIGHT);
    uint8_t bot_bits = (uint8_t)(GLCD_PAGE_HEIGHT - 1u - (y_end % GLCD_PAGE_HEIGHT));

    for (uint8_t page = first_page; page <= last_page; page++) {
        uint8_t mask = 0xFF;
        if (page == first_page) mask &= (uint8_t)(0xFF << top_bits);
        if (page == last_page)  mask &= (uint8_t)(0xFF >> bot_bits);
        for (uint8_t col = 0; col < w; col++) {
            apply_byte_mask((uint8_t)(x + col), page, mask, op);
        }
    }
}

void glcd_attach_buffer(uint8_t *buffer, uint8_t width_px, uint8_t height_px)
{
    g_buffer = buffer;
    g_width = width_px;
    g_height = height_px;
    g_pages = (uint8_t)((height_px + GLCD_PAGE_HEIGHT - 1u) / GLCD_PAGE_HEIGHT);
    g_cursor_x = 0;
    g_cursor_y = 0;
    g_inverted = false;
    g_font = NULL;
    g_font_mode = GLCD_OVERWRITE;
    if (g_buffer) memset(g_buffer, 0, (size_t)g_width * g_pages);
}

void glcd_clear(void)
{
    if (!g_buffer) return;
    memset(g_buffer, 0, (size_t)g_width * g_pages);
    g_cursor_x = 0;
    g_cursor_y = 0;
}

void glcd_clear_line(uint8_t row)
{
    if (row >= g_pages) return;
    memset(&g_buffer[row * g_width], 0, g_width);
}

void glcd_fill_screen(glcd_color color)
{
    if (!g_buffer) return;
    memset(g_buffer, color == GLCD_BLACK ? 0xFF : 0x00, (size_t)g_width * g_pages);
}

void glcd_goto(uint8_t x, uint8_t y)
{
    // Clamp against the NEW arg (regression #3).
    if (x >= g_width)  x = (uint8_t)(g_width - 1);
    if (y >= g_height) y = (uint8_t)(g_height - 1);
    g_cursor_x = x;
    g_cursor_y = y;
}

void glcd_goto_line(uint8_t row)
{
    if (row >= g_pages) row = (uint8_t)(g_pages - 1);
    g_cursor_y = (uint8_t)(row * GLCD_PAGE_HEIGHT);
}

uint8_t glcd_get_x(void) { return g_cursor_x; }
uint8_t glcd_get_y(void) { return g_cursor_y; }
uint8_t glcd_get_line(void) { return (uint8_t)(g_cursor_y / GLCD_PAGE_HEIGHT); }

void glcd_set_pixel(uint8_t x, uint8_t y, glcd_color color)
{
    // Direct buffer index. No cursor side effects (regression #1).
    if (!g_buffer || x >= g_width || y >= g_height) return;
    uint16_t idx = (uint16_t)(y / GLCD_PAGE_HEIGHT) * g_width + x;
    uint8_t mask = (uint8_t)(1u << (y % GLCD_PAGE_HEIGHT));
    if (color == GLCD_BLACK) g_buffer[idx] |= mask;
    else                     g_buffer[idx] &= (uint8_t)~mask;
}

void glcd_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, glcd_color color)
{
    if (!g_buffer) return;
    if (y0 == y1) {
        // Horizontal line optimization using row masks when possible.
        uint8_t a = min_u8(x0, x1);
        uint8_t b = max_u8(x0, x1);
        if (a >= g_width || y0 >= g_height) return;
        if (b >= g_width) b = (uint8_t)(g_width - 1);
        apply_rect(a, y0, (uint8_t)(b - a + 1), 1, color == GLCD_BLACK ? MASK_SET : MASK_CLEAR);
        return;
    }
    if (x0 == x1) {
        uint8_t a = min_u8(y0, y1);
        uint8_t b = max_u8(y0, y1);
        if (x0 >= g_width || a >= g_height) return;
        if (b >= g_height) b = (uint8_t)(g_height - 1);
        apply_rect(x0, a, 1, (uint8_t)(b - a + 1), color == GLCD_BLACK ? MASK_SET : MASK_CLEAR);
        return;
    }

    // Bresenham for the diagonal case. Use int16_t throughout so sign is safe.
    int16_t ix0 = x0, iy0 = y0, ix1 = x1, iy1 = y1;
    int16_t dx = abs_i16((int16_t)(ix1 - ix0));
    int16_t dy = -abs_i16((int16_t)(iy1 - iy0));
    int16_t sx = ix0 < ix1 ? 1 : -1;
    int16_t sy = iy0 < iy1 ? 1 : -1;
    int16_t err = dx + dy;

    while (1) {
        if (ix0 >= 0 && iy0 >= 0 && ix0 < g_width && iy0 < g_height) {
            glcd_set_pixel((uint8_t)ix0, (uint8_t)iy0, color);
        }
        if (ix0 == ix1 && iy0 == iy1) break;
        int16_t e2 = (int16_t)(err * 2);
        if (e2 >= dy) { err += dy; ix0 += sx; }
        if (e2 <= dx) { err += dx; iy0 += sy; }
    }
}

void glcd_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, glcd_color color)
{
    if (w == 0 || h == 0) return;
    uint8_t x1 = (uint8_t)(x + w - 1);
    uint8_t y1 = (uint8_t)(y + h - 1);
    glcd_draw_line(x,  y,  x1, y,  color);
    glcd_draw_line(x,  y1, x1, y1, color);
    glcd_draw_line(x,  y,  x,  y1, color);
    glcd_draw_line(x1, y,  x1, y1, color);
}

static void circle_pixels(uint8_t cx, uint8_t cy, int16_t x, int16_t y, glcd_color color)
{
    int16_t icx = cx, icy = cy;
    int16_t pts[8][2] = {
        { icx + x, icy + y }, { icx - x, icy + y },
        { icx + x, icy - y }, { icx - x, icy - y },
        { icx + y, icy + x }, { icx - y, icy + x },
        { icx + y, icy - x }, { icx - y, icy - x },
    };
    for (uint8_t i = 0; i < 8; i++) {
        int16_t px = pts[i][0], py = pts[i][1];
        if (px >= 0 && py >= 0 && px < g_width && py < g_height) {
            glcd_set_pixel((uint8_t)px, (uint8_t)py, color);
        }
    }
}

void glcd_draw_circle(uint8_t cx, uint8_t cy, uint8_t radius, glcd_color color)
{
    // Bresenham mid-point. Each candidate pixel is clipped individually
    // (regression #7 — the old version demanded cx >= radius).
    if (!g_buffer || radius == 0) {
        if (g_buffer && radius == 0) glcd_set_pixel(cx, cy, color);
        return;
    }
    int16_t x = 0;
    int16_t y = radius;
    int16_t d = (int16_t)(1 - (int16_t)radius);
    while (x <= y) {
        circle_pixels(cx, cy, x, y, color);
        x++;
        if (d < 0) {
            d += (int16_t)(2 * x + 1);
        } else {
            y--;
            d += (int16_t)(2 * (x - y) + 1);
        }
    }
}

void glcd_draw_round_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t radius, glcd_color color)
{
    if (w == 0 || h == 0) return;
    uint8_t half = (uint8_t)((w < h ? w : h) / 2);
    if (radius > half) radius = half;
    uint8_t x1 = (uint8_t)(x + w - 1);
    uint8_t y1 = (uint8_t)(y + h - 1);

    // Straight edges.
    glcd_draw_line((uint8_t)(x + radius), y,  (uint8_t)(x1 - radius), y,  color);
    glcd_draw_line((uint8_t)(x + radius), y1, (uint8_t)(x1 - radius), y1, color);
    glcd_draw_line(x,  (uint8_t)(y + radius), x,  (uint8_t)(y1 - radius), color);
    glcd_draw_line(x1, (uint8_t)(y + radius), x1, (uint8_t)(y1 - radius), color);

    // Rounded corners via a mid-point sweep.
    int16_t cx = 0, cy = radius;
    int16_t d = (int16_t)(1 - (int16_t)radius);
    int16_t lx = (int16_t)x + radius;
    int16_t rx = (int16_t)x1 - radius;
    int16_t ty = (int16_t)y + radius;
    int16_t by = (int16_t)y1 - radius;
    while (cx <= cy) {
        int16_t pts[8][2] = {
            { lx - cx, ty - cy }, { lx - cy, ty - cx },
            { rx + cx, ty - cy }, { rx + cy, ty - cx },
            { lx - cx, by + cy }, { lx - cy, by + cx },
            { rx + cx, by + cy }, { rx + cy, by + cx },
        };
        for (uint8_t i = 0; i < 8; i++) {
            int16_t px = pts[i][0], py = pts[i][1];
            if (px >= 0 && py >= 0 && px < g_width && py < g_height) {
                glcd_set_pixel((uint8_t)px, (uint8_t)py, color);
            }
        }
        cx++;
        if (d < 0) d += (int16_t)(2 * cx + 1);
        else { cy--; d += (int16_t)(2 * (cx - cy) + 1); }
    }
}

void glcd_draw_triangle(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, glcd_color color)
{
    glcd_draw_line(x0, y0, x1, y1, color);
    glcd_draw_line(x1, y1, x2, y2, color);
    glcd_draw_line(x2, y2, x0, y0, color);
}

void glcd_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, glcd_color color)
{
    apply_rect(x, y, w, h, color == GLCD_BLACK ? MASK_SET : MASK_CLEAR);
}

void glcd_fill_round_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t radius, glcd_color color)
{
    if (w == 0 || h == 0) return;
    uint8_t half = (uint8_t)((w < h ? w : h) / 2);
    if (radius > half) radius = half;

    // Middle slab between the two curved rows.
    if (h > 2 * radius) {
        glcd_fill_rect(x, (uint8_t)(y + radius), w, (uint8_t)(h - 2 * radius), color);
    }

    // Curved top and bottom via scanlines produced by a mid-point sweep.
    int16_t cx = 0, cy = radius;
    int16_t d = (int16_t)(1 - (int16_t)radius);
    int16_t lx = (int16_t)x + radius;
    int16_t rx = (int16_t)x + w - 1 - radius;
    int16_t ty = (int16_t)y + radius;
    int16_t by = (int16_t)y + h - 1 - radius;
    while (cx <= cy) {
        int16_t sw_x = lx - cy;
        int16_t sw_r = rx + cy;
        int16_t sn_x = lx - cx;
        int16_t sn_r = rx + cx;
        if (sw_x < 0) sw_x = 0;
        if (sn_x < 0) sn_x = 0;
        if (sw_r >= g_width) sw_r = (int16_t)(g_width - 1);
        if (sn_r >= g_width) sn_r = (int16_t)(g_width - 1);
        uint8_t sw_w = (uint8_t)(sw_r - sw_x + 1);
        uint8_t sn_w = (uint8_t)(sn_r - sn_x + 1);

        // Top cap.
        if (ty - cy >= 0 && ty - cy < g_height) {
            glcd_fill_rect((uint8_t)sw_x, (uint8_t)(ty - cy), sw_w, 1, color);
        }
        if (ty - cx >= 0 && ty - cx < g_height) {
            glcd_fill_rect((uint8_t)sn_x, (uint8_t)(ty - cx), sn_w, 1, color);
        }
        // Bottom cap.
        if (by + cy < g_height) {
            glcd_fill_rect((uint8_t)sw_x, (uint8_t)(by + cy), sw_w, 1, color);
        }
        if (by + cx < g_height) {
            glcd_fill_rect((uint8_t)sn_x, (uint8_t)(by + cx), sn_w, 1, color);
        }

        cx++;
        if (d < 0) d += (int16_t)(2 * cx + 1);
        else { cy--; d += (int16_t)(2 * (cx - cy) + 1); }
    }
}

static void swap_i16(int16_t *a, int16_t *b) { int16_t t = *a; *a = *b; *b = t; }

void glcd_fill_triangle(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, glcd_color color)
{
    // Integer scanline fill. Handles horizontal edges with no division
    // (regression #5 — the old version divided by (y2 - y1) and crashed).
    int16_t ax = x0, ay = y0, bx = x1, by = y1, cx = x2, cy = y2;
    if (ay > by) { swap_i16(&ax, &bx); swap_i16(&ay, &by); }
    if (by > cy) { swap_i16(&bx, &cx); swap_i16(&by, &cy); }
    if (ay > by) { swap_i16(&ax, &bx); swap_i16(&ay, &by); }

    // Fully degenerate (all three colinear horizontally): draw a line.
    if (ay == cy) {
        int16_t xmin = ax, xmax = ax;
        if (bx < xmin) xmin = bx;
        if (bx > xmax) xmax = bx;
        if (cx < xmin) xmin = cx;
        if (cx > xmax) xmax = cx;
        glcd_draw_line((uint8_t)xmin, (uint8_t)ay, (uint8_t)xmax, (uint8_t)ay, color);
        return;
    }

    int16_t total_h = cy - ay;
    for (int16_t y = ay; y <= cy; y++) {
        bool upper = (y < by) || (ay == by);
        int16_t seg_h = upper ? (int16_t)(by - ay) : (int16_t)(cy - by);
        if (seg_h == 0) seg_h = 1;
        int16_t sa = upper ? (int16_t)(((int32_t)(y - ay) * (bx - ax)) / seg_h)
                           : (int16_t)(((int32_t)(y - by) * (cx - bx)) / seg_h);
        int16_t sb = (int16_t)(((int32_t)(y - ay) * (cx - ax)) / total_h);
        int16_t xa = ax + sa;
        int16_t xb = ax + sb;
        if (!upper) xa = bx + sa;
        if (xa > xb) { int16_t t = xa; xa = xb; xb = t; }
        if (xa < 0) xa = 0;
        if (xb >= g_width) xb = (int16_t)(g_width - 1);
        if (y >= 0 && y < g_height && xa <= xb) {
            glcd_fill_rect((uint8_t)xa, (uint8_t)y, (uint8_t)(xb - xa + 1), 1, color);
        }
    }
}

void glcd_fill_circle(uint8_t cx, uint8_t cy, uint8_t radius, glcd_color color)
{
    if (!g_buffer) return;
    if (radius == 0) { glcd_set_pixel(cx, cy, color); return; }

    // Mid-point sweep that emits horizontal scanlines. Each span is clipped
    // by fill_rect so no per-pixel bound check is needed (regression #7).
    int16_t x = 0, y = radius;
    int16_t d = (int16_t)(1 - (int16_t)radius);
    while (x <= y) {
        int16_t x_left_wide = (int16_t)cx - y;
        int16_t x_right_wide = (int16_t)cx + y;
        int16_t x_left_narrow = (int16_t)cx - x;
        int16_t x_right_narrow = (int16_t)cx + x;
        int16_t y_up_wide = (int16_t)cy - x;
        int16_t y_dn_wide = (int16_t)cy + x;
        int16_t y_up_narrow = (int16_t)cy - y;
        int16_t y_dn_narrow = (int16_t)cy + y;

        if (x_left_wide < 0) x_left_wide = 0;
        if (x_right_wide >= g_width) x_right_wide = (int16_t)(g_width - 1);
        if (x_left_narrow < 0) x_left_narrow = 0;
        if (x_right_narrow >= g_width) x_right_narrow = (int16_t)(g_width - 1);

        if (y_up_wide >= 0 && y_up_wide < g_height) {
            glcd_fill_rect((uint8_t)x_left_wide, (uint8_t)y_up_wide,
                           (uint8_t)(x_right_wide - x_left_wide + 1), 1, color);
        }
        if (y_dn_wide >= 0 && y_dn_wide < g_height && y_dn_wide != y_up_wide) {
            glcd_fill_rect((uint8_t)x_left_wide, (uint8_t)y_dn_wide,
                           (uint8_t)(x_right_wide - x_left_wide + 1), 1, color);
        }
        if (y_up_narrow >= 0 && y_up_narrow < g_height) {
            glcd_fill_rect((uint8_t)x_left_narrow, (uint8_t)y_up_narrow,
                           (uint8_t)(x_right_narrow - x_left_narrow + 1), 1, color);
        }
        if (y_dn_narrow >= 0 && y_dn_narrow < g_height && y_dn_narrow != y_up_narrow) {
            glcd_fill_rect((uint8_t)x_left_narrow, (uint8_t)y_dn_narrow,
                           (uint8_t)(x_right_narrow - x_left_narrow + 1), 1, color);
        }

        x++;
        if (d < 0) d += (int16_t)(2 * x + 1);
        else { y--; d += (int16_t)(2 * (x - y) + 1); }
    }
}

void glcd_set_inverted(bool inverted) { g_inverted = inverted; }
bool glcd_is_inverted(void) { return g_inverted; }

void glcd_invert_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    apply_rect(x, y, w, h, MASK_INVERT);
}

void glcd_draw_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bitmap_pgm, glcd_color color)
{
    if (!g_buffer || !bitmap_pgm || w == 0 || h == 0) return;
    if (x >= g_width || y >= g_height) return;
    uint8_t rows = (uint8_t)((h + GLCD_PAGE_HEIGHT - 1u) / GLCD_PAGE_HEIGHT);
    uint16_t y_end = (uint16_t)y + h;
    for (uint8_t ry = 0; ry < rows; ry++) {
        for (uint8_t col = 0; col < w; col++) {
            uint8_t bits = pgm_read_byte(&bitmap_pgm[(uint16_t)ry * w + col]);
            for (uint8_t bit = 0; bit < GLCD_PAGE_HEIGHT; bit++) {
                uint16_t py = (uint16_t)y + (uint16_t)ry * GLCD_PAGE_HEIGHT + bit;
                if (py >= y_end) break;
                if (bits & (uint8_t)(1u << bit)) {
                    glcd_set_pixel((uint8_t)(x + col), (uint8_t)py, color);
                }
            }
        }
    }
}

void glcd_set_font(const glcd_font *font, glcd_text_mode mode)
{
    g_font = font;
    g_font_mode = mode;
}

// Per-glyph byte stride. The first byte is the width; the remaining bytes
// are width_max * lines column bytes.
static uint8_t font_lines(void)
{
    if (!g_font) return 0;
    return (uint8_t)((g_font->height + GLCD_PAGE_HEIGHT - 1u) / GLCD_PAGE_HEIGHT);
}

static const uint8_t *glyph_ptr(char c)
{
    if (!g_font) return NULL;
    if ((uint8_t)c < g_font->first) return NULL;
    uint8_t idx = (uint8_t)((uint8_t)c - g_font->first);
    if (idx >= g_font->count) return NULL;
    uint16_t stride = (uint16_t)g_font->width * font_lines() + 1u;
    return &g_font->data[(uint16_t)idx * stride];
}

uint8_t glcd_get_char_width(char c)
{
    const uint8_t *p = glyph_ptr(c);
    if (!p) return 0;
    return pgm_read_byte(p);
}

uint8_t glcd_get_string_width(const char *s)
{
    if (!s) return 0;
    uint16_t total = 0;
    while (*s) {
        total += glcd_get_char_width(*s++);
        total += 1u; // Inter-character spacing (matches old behavior).
    }
    if (total == 0) return 0;
    total -= 1u;
    return total > 255u ? 255u : (uint8_t)total;
}

uint8_t glcd_get_string_width_p(const char *s_pgm)
{
    if (!s_pgm) return 0;
    uint16_t total = 0;
    char ch;
    while ((ch = (char)pgm_read_byte(s_pgm++)) != '\0') {
        total += glcd_get_char_width(ch);
        total += 1u;
    }
    if (total == 0) return 0;
    total -= 1u;
    return total > 255u ? 255u : (uint8_t)total;
}

// Blit one glyph starting at the cursor. The font payload is column-major
// with "lines" (ceil(height/8)) bytes per column; each byte owns a full
// 8-pixel vertical slice. When the cursor sits mid-page the slice is shifted
// across two framebuffer pages. Advances the cursor by (glyph_width + 1).
static void draw_glyph(char c)
{
    const uint8_t *p = glyph_ptr(c);
    if (!p) return;
    uint8_t w = pgm_read_byte(p++);
    uint8_t lines = font_lines();
    uint8_t x = g_cursor_x;
    uint8_t y = g_cursor_y;
    if (x >= g_width) return;

    uint8_t overflow = (uint8_t)(y % GLCD_PAGE_HEIGHT);
    uint8_t top_page = (uint8_t)(y / GLCD_PAGE_HEIGHT);

    // Clip width against the right edge of the panel.
    if ((uint16_t)x + w > g_width) {
        w = (uint8_t)(g_width - x);
    }

    uint8_t keep_lo = overflow ? (uint8_t)((1u << overflow) - 1u) : 0u;
    uint8_t keep_hi = (uint8_t)~keep_lo;

    for (uint8_t line = 0; line < lines; line++) {
        uint8_t page = (uint8_t)(top_page + line);
        if (page >= g_pages) break;
        for (uint8_t col = 0; col < w; col++) {
            uint8_t glyph_byte = pgm_read_byte(&p[(uint16_t)col * lines + line]);
            uint16_t idx = (uint16_t)page * g_width + (uint8_t)(x + col);
            uint8_t upper = (uint8_t)(glyph_byte << overflow);
            if (g_font_mode == GLCD_OVERWRITE) {
                // Overwrite the glyph's slice; preserve bits below it.
                g_buffer[idx] = (uint8_t)((g_buffer[idx] & keep_lo) | upper);
            } else {
                g_buffer[idx] |= upper;
            }
            if (overflow && page + 1u < g_pages) {
                uint8_t lower = (uint8_t)(glyph_byte >> (GLCD_PAGE_HEIGHT - overflow));
                uint16_t idx2 = idx + g_width;
                if (g_font_mode == GLCD_OVERWRITE) {
                    g_buffer[idx2] = (uint8_t)((g_buffer[idx2] & keep_hi) | lower);
                } else {
                    g_buffer[idx2] |= lower;
                }
            }
        }
    }

    // Trailing blank column — only if it still fits (regression #2).
    uint8_t trail_x = (uint8_t)(x + w);
    if (trail_x < g_width && g_font_mode == GLCD_OVERWRITE) {
        for (uint8_t line = 0; line < lines; line++) {
            uint8_t page = (uint8_t)(top_page + line);
            if (page >= g_pages) break;
            uint16_t idx = (uint16_t)page * g_width + trail_x;
            g_buffer[idx] &= keep_lo;
            if (overflow && page + 1u < g_pages) {
                g_buffer[idx + g_width] &= keep_hi;
            }
        }
    }

    // Advance cursor past the glyph plus one spacing column.
    uint16_t new_x = (uint16_t)x + w + 1u;
    if (new_x >= g_width) new_x = g_width - 1u;
    g_cursor_x = (uint8_t)new_x;
}

void glcd_print_char(char c)
{
    draw_glyph(c);
}

void glcd_print_string(const char *s)
{
    if (!s) return;
    while (*s) {
        if (g_cursor_x >= g_width) break;
        draw_glyph(*s++);
    }
}

void glcd_print_string_p(const char *s_pgm)
{
    if (!s_pgm) return;
    char ch;
    while ((ch = (char)pgm_read_byte(s_pgm++)) != '\0') {
        if (g_cursor_x >= g_width) break;
        draw_glyph(ch);
    }
}

void glcd_print_int(int32_t value)
{
    // Buffer sized for "-2147483648\0" (regression #9 — the old Int2bcd
    // guarded against the one value whose |x| doesn't fit, so INT32_MIN
    // printed nothing).
    char buf[12];
    char *p = &buf[sizeof buf - 1];
    *p = '\0';

    bool negative = (value < 0);
    // Convert to magnitude in uint32_t. The 0u - (uint32_t)value trick
    // gives the correct magnitude for INT32_MIN too (defined wrap-around).
    uint32_t u = negative ? (0u - (uint32_t)value) : (uint32_t)value;
    if (u == 0) {
        *--p = '0';
    } else {
        while (u > 0) {
            *--p = (char)('0' + (u % 10u));
            u /= 10u;
        }
    }
    if (negative) *--p = '-';

    glcd_print_string(p);
}

void glcd_print_float(float value, uint8_t decimals)
{
    // Integer scaling with round-half-to-even and zero-padded fractional
    // part (regression #8 — the old version lost leading zeros in 1.05
    // and truncated 0.3 to 0.2).
    if (decimals > 6) decimals = 6;

    if (value < 0) {
        glcd_print_char('-');
        value = -value;
    }

    static const uint32_t pow10[] = {
        1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u,
    };
    uint32_t scale = pow10[decimals];
    float scaled = value * (float)scale;

    // Round-half-to-even. For positive values this is: floor + ties-to-even.
    uint32_t whole = (uint32_t)scaled;
    float frac = scaled - (float)whole;
    if (frac > 0.5f) {
        whole++;
    } else if (frac == 0.5f) {
        if (whole & 1u) whole++;
    }

    uint32_t int_part = whole / scale;
    uint32_t frac_part = whole - int_part * scale;

    glcd_print_int((int32_t)int_part);

    if (decimals > 0) {
        glcd_print_char('.');
        // Print leading zeros for the fractional part.
        uint32_t div = scale / 10u;
        while (div > 0) {
            char digit = (char)('0' + (frac_part / div) % 10u);
            glcd_print_char(digit);
            div /= 10u;
        }
    }
}

uint8_t *glcd_test_get_buffer(void) { return g_buffer; }
uint8_t glcd_test_get_width(void) { return g_width; }
uint8_t glcd_test_get_height(void) { return g_height; }

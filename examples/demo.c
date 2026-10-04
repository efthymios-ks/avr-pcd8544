// PCD8544 (Nokia 5110) demo: text, shapes, and inversion toggle. Target
// ATmega328P DIP-28 (default pin map in pcd8544_config.h). Also builds for
// atmega32.
//
// Wiring (see README.md for the full table): RST=PB0, DC=PB1, SCE=PB2 (SS),
// DIN=PB3 (MOSI), SCLK=PB5 (SCK). The breakout runs on 3.3 V logic — add
// level-shifters if driving from a 5 V AVR. Backlight (LED pin) through a
// series resistor to +3.3 V.

#include <util/delay.h>
#include <avr/pgmspace.h>
#include <stdbool.h>

#include "pcd8544.h"
#include "glcd.h"
#include "fonts/font_5x8.h"

static const char PROGMEM msg_title[] = "avr-pcd8544 v2";

int main(void)
{
    pcd8544_init();
    glcd_clear();

    // Title in the small 5x8 font along the top line.
    glcd_set_font(&font_5x8, GLCD_OVERWRITE);
    glcd_goto(0, 0);
    glcd_print_string_p(msg_title);

    // Shape sampler across the lower rows.
    glcd_draw_rect(2, 16, 24, 24, GLCD_BLACK);
    glcd_draw_circle(45, 28, 10, GLCD_BLACK);
    glcd_draw_triangle(62, 40, 82, 40, 72, 20, GLCD_BLACK);

    // Border around the whole screen.
    glcd_draw_rect(0, 0, PCD8544_WIDTH, PCD8544_HEIGHT, GLCD_BLACK);

    glcd_render();

    bool invert = false;
    while (1) {
        _delay_ms(1000);
        invert = !invert;
        glcd_set_inverted(invert);
        glcd_render();
    }
}

#include "pcd8544.h"
#include "pcd8544_internal.h"
#include "io_macros.h"
#include "glcd.h"

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>

// PCD8544 (Nokia 5110) chip layer. The 84x48 panel is driven serially: DIN
// clocked on SCLK's rising edge, DC selects command vs data, SCE enables the
// controller. All drawing is done into the shared framebuffer by the glcd
// core; glcd_render() blits the 504-byte buffer to the panel in one burst.

// Command set from the PCD8544 datasheet.
#define PCD8544_CMD_FUNCTION_SET        0x20u
#define PCD8544_FS_POWER_DOWN           0x04u
#define PCD8544_FS_VERTICAL_ADDRESSING  0x02u
#define PCD8544_FS_EXTENDED_INSTRUCTION 0x01u

// Basic instruction set (H=0).
#define PCD8544_CMD_DISPLAY_CONTROL     0x08u
#define PCD8544_DC_BLANK                0x00u
#define PCD8544_DC_NORMAL               0x04u
#define PCD8544_DC_ALL_ON               0x01u
#define PCD8544_DC_INVERSE              0x05u
#define PCD8544_CMD_SET_Y_ADDR          0x40u
#define PCD8544_CMD_SET_X_ADDR          0x80u

// Extended instruction set (H=1).
#define PCD8544_CMD_TEMP_CONTROL        0x04u
#define PCD8544_TEMP_COEFF_2            0x02u
#define PCD8544_CMD_BIAS                0x10u
#define PCD8544_BIAS_1_48               0x03u
#define PCD8544_CMD_SET_VOP             0x80u
#define PCD8544_VOP_MASK                0x7Fu

#define PCD8544_PAGES  (PCD8544_HEIGHT / 8u)
#define PCD8544_BYTES  ((uint16_t)PCD8544_WIDTH * PCD8544_PAGES)

#if (PCD8544_WIDTH * PCD8544_HEIGHT) == 0
#error "PCD8544_WIDTH and PCD8544_HEIGHT must both be non-zero."
#endif

#if (PCD8544_USE_SPI == 0) && (F_CPU > 20000000UL)
#error "Bit-bang SCLK would exceed 4 MHz; enable PCD8544_USE_SPI or lower F_CPU."
#endif

static uint8_t g_fb[PCD8544_BYTES];

// Test-only bus capture.
static uint8_t  *g_cap_buf;
static uint16_t  g_cap_cap;
static uint16_t  g_cap_count;

static inline void cap_record(uint8_t is_data, uint8_t value)
{
    if (!g_cap_buf) return;
    if (g_cap_count >= g_cap_cap) return;
    g_cap_buf[g_cap_count * 2u]      = (uint8_t)(is_data ? 0x80u : 0x00u);
    g_cap_buf[g_cap_count * 2u + 1u] = value;
    g_cap_count++;
}

#if PCD8544_USE_SPI
static inline void spi_init(void)
{
    // SS must be an output or SPI will drop out of master mode. The config
    // leaves SCE on PB2 which is also SS on the ATmega328P, so it's already
    // an output.
    SPCR = (uint8_t)((1u << SPE) | (1u << MSTR));
    SPSR = (uint8_t)(1u << SPI2X);
}

static inline void shift_out(uint8_t value)
{
    SPDR = value;
    while (!(SPSR & (uint8_t)(1u << SPIF))) { }
    (void)SPDR;
}
#else
static inline void shift_out(uint8_t value)
{
    // Bit-bang. MSB first on SCLK's rising edge.
    for (int8_t bit = 7; bit >= 0; bit--) {
        IO_WRITE(PCD8544_PIN_DIN, (uint8_t)((value >> bit) & 1u));
        IO_WRITE(PCD8544_PIN_SCLK, IO_HIGH);
        IO_WRITE(PCD8544_PIN_SCLK, IO_LOW);
    }
}
#endif

static void send_command(uint8_t cmd)
{
    IO_WRITE(PCD8544_PIN_DC, IO_LOW);
    IO_WRITE(PCD8544_PIN_SCE, IO_LOW);
    shift_out(cmd);
    IO_WRITE(PCD8544_PIN_SCE, IO_HIGH);
    cap_record(0, cmd);
}

void pcd8544_init(void)
{
    IO_MODE(PCD8544_PIN_RST, IO_OUTPUT);
    IO_MODE(PCD8544_PIN_DC, IO_OUTPUT);
    IO_MODE(PCD8544_PIN_SCE, IO_OUTPUT);
    IO_MODE(PCD8544_PIN_DIN, IO_OUTPUT);
    IO_MODE(PCD8544_PIN_SCLK, IO_OUTPUT);

    IO_WRITE(PCD8544_PIN_DC, IO_LOW);
    IO_WRITE(PCD8544_PIN_DIN, IO_LOW);
    IO_WRITE(PCD8544_PIN_SCLK, IO_LOW);
    IO_WRITE(PCD8544_PIN_SCE, IO_HIGH);

    // Reset pulse. The datasheet requires RST to be pulled low within 30 ms
    // of VDD stabilizing; the delays here are comfortably inside that window.
    IO_WRITE(PCD8544_PIN_RST, IO_HIGH);
    _delay_ms(1);
    IO_WRITE(PCD8544_PIN_RST, IO_LOW);
    _delay_us(100);
    IO_WRITE(PCD8544_PIN_RST, IO_HIGH);

#if PCD8544_USE_SPI
    spi_init();
#endif

    // Minimal init sequence per the datasheet. v1 shipped three extra
    // display-control writes (all-on, blank, normal) that were just visual
    // noise during startup; we go straight to normal mode.
    send_command((uint8_t)(PCD8544_CMD_FUNCTION_SET | PCD8544_FS_EXTENDED_INSTRUCTION));
    send_command((uint8_t)(PCD8544_CMD_SET_VOP | (PCD8544_DEFAULT_CONTRAST & PCD8544_VOP_MASK)));
    send_command((uint8_t)(PCD8544_CMD_BIAS | PCD8544_BIAS_1_48));
    send_command((uint8_t)(PCD8544_CMD_TEMP_CONTROL | PCD8544_TEMP_COEFF_2));
    send_command((uint8_t)PCD8544_CMD_FUNCTION_SET);
    send_command((uint8_t)(PCD8544_CMD_DISPLAY_CONTROL | PCD8544_DC_NORMAL));

    glcd_attach_buffer(g_fb, PCD8544_WIDTH, PCD8544_HEIGHT);
}

void pcd8544_set_contrast(uint8_t vop)
{
    if (vop > PCD8544_VOP_MASK) vop = PCD8544_VOP_MASK;
    send_command((uint8_t)(PCD8544_CMD_FUNCTION_SET | PCD8544_FS_EXTENDED_INSTRUCTION));
    send_command((uint8_t)(PCD8544_CMD_SET_VOP | vop));
    send_command((uint8_t)PCD8544_CMD_FUNCTION_SET);
}

void glcd_render(void)
{
    // Save the glcd cursor so we don't trample it mid-render (bug #3).
    uint8_t saved_x = glcd_get_x();
    uint8_t saved_y = glcd_get_y();
    bool invert = glcd_is_inverted();

    // Set X/Y address once; horizontal addressing mode auto-increments across
    // the entire 504-byte burst so we can hold SCE low for the whole thing.
    send_command((uint8_t)PCD8544_CMD_SET_Y_ADDR);
    send_command((uint8_t)PCD8544_CMD_SET_X_ADDR);

    IO_WRITE(PCD8544_PIN_DC, IO_HIGH);
    IO_WRITE(PCD8544_PIN_SCE, IO_LOW);
    for (uint16_t i = 0; i < PCD8544_BYTES; i++) {
        uint8_t byte = g_fb[i];
        if (invert) byte = (uint8_t)~byte;
        shift_out(byte);
        cap_record(1, byte);
    }
    IO_WRITE(PCD8544_PIN_SCE, IO_HIGH);

    glcd_goto(saved_x, saved_y);
}

void pcd8544_test_capture_begin(uint8_t *buffer, uint16_t capacity)
{
    g_cap_buf = buffer;
    g_cap_cap = capacity;
    g_cap_count = 0;
}

uint16_t pcd8544_test_capture_end(void)
{
    uint16_t count = g_cap_count;
    g_cap_buf = (uint8_t *)0;
    g_cap_cap = 0;
    g_cap_count = 0;
    return count;
}

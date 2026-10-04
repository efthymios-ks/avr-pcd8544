#include "unity.h"
#include "avr/io.h"
#include "pcd8544.h"
#include "pcd8544_internal.h"
#include "glcd.h"

void setUp(void) {}
void tearDown(void) {}


#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Datasheet command byte values that the init sequence must emit.
#define EXPECTED_FUNCTION_SET_H1  0x21u
#define EXPECTED_FUNCTION_SET_H0  0x20u
#define EXPECTED_BIAS_1_48        0x13u
#define EXPECTED_TEMP_COEFF_2     0x06u
#define EXPECTED_DISPLAY_NORMAL   0x0Cu
#define EXPECTED_SET_VOP_DEFAULT  (0x80u | 60u)
#define EXPECTED_SET_VOP_63       (0x80u | 0x3Fu)

#define LOG_ENTRIES  2048u

static uint8_t log_buf[LOG_ENTRIES * 2u];

static void setup(void)
{
    fake_io_reset();
    memset(log_buf, 0, sizeof log_buf);
    pcd8544_test_capture_begin(log_buf, LOG_ENTRIES);
}

// Returns true when the capture log contains a command byte equal to value.
static bool log_contains_cmd(uint16_t count, uint8_t value)
{
    for (uint16_t i = 0; i < count; i++) {
        uint8_t flags = log_buf[i * 2u];
        uint8_t byte = log_buf[i * 2u + 1u];
        if ((flags & 0x80u) == 0u && byte == value) return true;
    }
    return false;
}

static void init_emits_the_datasheet_command_sequence(void)
{
    setup();
    pcd8544_init();
    uint16_t count = pcd8544_test_capture_end();
    TEST_ASSERT_TRUE(log_contains_cmd(count, EXPECTED_FUNCTION_SET_H1));
    TEST_ASSERT_TRUE(log_contains_cmd(count, EXPECTED_SET_VOP_DEFAULT));
    TEST_ASSERT_TRUE(log_contains_cmd(count, EXPECTED_BIAS_1_48));
    TEST_ASSERT_TRUE(log_contains_cmd(count, EXPECTED_TEMP_COEFF_2));
    TEST_ASSERT_TRUE(log_contains_cmd(count, EXPECTED_FUNCTION_SET_H0));
    TEST_ASSERT_TRUE(log_contains_cmd(count, EXPECTED_DISPLAY_NORMAL));
}

static void set_contrast_sends_vop_command(void)
{
    setup();
    pcd8544_init();
    pcd8544_test_capture_begin(log_buf, LOG_ENTRIES);
    pcd8544_set_contrast(0x3Fu);
    uint16_t count = pcd8544_test_capture_end();
    TEST_ASSERT_TRUE(log_contains_cmd(count, EXPECTED_SET_VOP_63));
}

static void render_emits_504_data_bytes_all_0xFF_when_filled_black(void)
{
    setup();
    pcd8544_init();
    glcd_fill_screen(GLCD_BLACK);
    pcd8544_test_capture_begin(log_buf, LOG_ENTRIES);
    glcd_render();
    uint16_t count = pcd8544_test_capture_end();

    uint16_t data_count = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t flags = log_buf[i * 2u];
        if ((flags & 0x80u) == 0u) continue;
        TEST_ASSERT_EQUAL_UINT8(0xFFu, log_buf[i * 2u + 1u]);
        data_count++;
    }
    TEST_ASSERT_EQUAL_UINT16(504u, data_count);
}

static void render_preserves_the_glcd_cursor(void)
{
    // Chip layer bug #3: the old render overwrote the user's text cursor.
    setup();
    pcd8544_init();
    glcd_goto(5, 10);
    pcd8544_test_capture_begin(log_buf, LOG_ENTRIES);
    glcd_render();
    (void)pcd8544_test_capture_end();
    TEST_ASSERT_EQUAL_UINT8(5u, glcd_get_x());
    TEST_ASSERT_EQUAL_UINT8(10u, glcd_get_y());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(init_emits_the_datasheet_command_sequence);
    RUN_TEST(set_contrast_sends_vop_command);
    RUN_TEST(render_emits_504_data_bytes_all_0xFF_when_filled_black);
    RUN_TEST(render_preserves_the_glcd_cursor);
    return UNITY_END();
}

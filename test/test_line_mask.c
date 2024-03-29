#include <esp_heap_caps.h>
#include <stdint.h>
#include <sys/types.h>
#include <unity.h>


void _epd_populate_line_mask(uint8_t* line_mask, const uint8_t* dirty_columns, int mask_len);

const uint8_t col_dirtyness_example[8] =  {0x00, 0x0F, 0x00, 0x11, 0xFF, 0xFF, 0x00, 0x80};


TEST_CASE("mask populated correctly", "[epdiy,unit]") {
    const uint8_t expected_mask[8] = {0x30, 0xF0, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00};
    uint8_t mask[8] = {0};
    _epd_populate_line_mask(mask, col_dirtyness_example, 4);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_mask, mask, 8);
}

TEST_CASE("neutral mask with null dirtyness", "[epdiy,unit]") {
    const uint8_t expected_mask[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
    uint8_t mask[8] = {0};
    
    _epd_populate_line_mask(mask, NULL, 4);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_mask, mask, 8);
}

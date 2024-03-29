#include <esp_heap_caps.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unity.h>


bool _epd_interlace_line(
    const uint8_t* to,
    const uint8_t* from,
    uint8_t* interlaced,
    uint8_t* col_dirtyness,
    int fb_width
);

static const uint8_t from_pattern[8] = {0xFF, 0xF0, 0x0F, 0x01, 0x55, 0xAA, 0xFF, 0x80};
static const uint8_t to_pattern[8] = {0xFF, 0xFF, 0x0F, 0x10, 0xAA, 0x55, 0xFF, 0x00};

static const uint8_t expected_interlaced_pattern[16] = {
    0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0x00, 0x01, 0x10, 0xA5, 0xA5, 0x5A, 0x5A, 0xFF, 0xFF, 0x00, 0x08};
static const uint8_t expected_col_dirtyness_pattern[8] = {0x00, 0x0F, 0x00, 0x11,
                                                          0xFF, 0xFF, 0x00, 0x80};

TEST_CASE("line diff works", "[epdiy,unit]") {
    int example_len = 176;
    bool dirty;

    uint8_t* from = heap_caps_aligned_alloc(16, example_len, MALLOC_CAP_DEFAULT);
    uint8_t* to = heap_caps_aligned_alloc(16, example_len, MALLOC_CAP_DEFAULT);
    uint8_t* interlaced = heap_caps_aligned_alloc(16, 2 * example_len, MALLOC_CAP_DEFAULT);
    uint8_t* col_dirtyness = heap_caps_aligned_alloc(16, example_len, MALLOC_CAP_DEFAULT);
    uint8_t* expected_interlaced = malloc(2 * example_len);
    uint8_t* expected_col_dirtyness = malloc(example_len);

    for (int i = 0; i < example_len / 8; i++) {
        memcpy(from + (8 * i), from_pattern, 8);
        memcpy(to + (8 * i), to_pattern, 8);
        memcpy(expected_interlaced + (16 * i), expected_interlaced_pattern, 16);
        memcpy(expected_col_dirtyness + (8 * i), expected_col_dirtyness_pattern, 8);
    }
    memset(col_dirtyness, 0, example_len);

    // This should trigger use of vector extensions on the S3
    TEST_ASSERT((uint32_t)to % 16 == 0)

    // fully aligned
    dirty = _epd_interlace_line(to, from, interlaced, col_dirtyness, 2 * example_len);

    TEST_ASSERT(dirty == true);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_col_dirtyness, col_dirtyness, example_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_interlaced, interlaced, 2 * example_len);

    // force an unaligned line end
    dirty = _epd_interlace_line(to, from, interlaced, col_dirtyness, 2 * (example_len - 8));

    TEST_ASSERT(dirty == true);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_col_dirtyness, col_dirtyness, example_len - 8);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_interlaced, interlaced, 2 * (example_len - 8));

    // force an unaligned line start
    dirty = _epd_interlace_line(
        to + 8, from + 8, interlaced + 16, col_dirtyness + 8, 2 * (example_len - 8)
    );

    TEST_ASSERT(dirty == true);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_col_dirtyness + 8, col_dirtyness + 8, example_len - 8);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_interlaced + 16, interlaced + 16, 2 * (example_len - 8));

    heap_caps_free(from);
    heap_caps_free(to);
    heap_caps_free(interlaced);
    heap_caps_free(col_dirtyness);
    free(expected_interlaced);
    free(expected_col_dirtyness);
}
#include <esp_heap_caps.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unity.h>
#include "esp_timer.h"

#define DEFAULT_EXAMPLE_LEN 704

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

typedef struct {
    uint8_t* from;
    uint8_t* to;
    uint8_t* interlaced;
    uint8_t* col_dirtyness;
    uint8_t* expected_interlaced;
    uint8_t* expected_col_dirtyness;
} DiffTestBuffers;

/**
 * (Re-)fill buffers with example data, clear result buffers.
 */
static void diff_test_buffers_fill(DiffTestBuffers* bufs, int example_len) {
    // initialize test and check patterns
    for (int i = 0; i < example_len / 8; i++) {
        memcpy(bufs->from + (8 * i), from_pattern, 8);
        memcpy(bufs->to + (8 * i), to_pattern, 8);
        memcpy(bufs->expected_interlaced + (16 * i), expected_interlaced_pattern, 16);
        memcpy(bufs->expected_col_dirtyness + (8 * i), expected_col_dirtyness_pattern, 8);
    }

    memset(bufs->col_dirtyness, 0, example_len);
    memset(bufs->interlaced, 0, example_len * 2);
}

/**
 * Allocates and populates buffers for diff tests.
 */
static void diff_test_buffers_init(DiffTestBuffers* bufs, int example_len) {
    bufs->from = heap_caps_aligned_alloc(16, example_len, MALLOC_CAP_DEFAULT);
    bufs->to = heap_caps_aligned_alloc(16, example_len, MALLOC_CAP_DEFAULT);
    bufs->interlaced = heap_caps_aligned_alloc(16, 2 * example_len, MALLOC_CAP_DEFAULT);
    bufs->col_dirtyness = heap_caps_aligned_alloc(16, example_len, MALLOC_CAP_DEFAULT);
    bufs->expected_interlaced = malloc(2 * example_len);
    bufs->expected_col_dirtyness = malloc(example_len);

    diff_test_buffers_fill(bufs, example_len);
}

/**
 * Free buffers used for diff testing.
 */
static void diff_test_buffers_free(DiffTestBuffers* bufs) {
    heap_caps_free(bufs->from);
    heap_caps_free(bufs->to);
    heap_caps_free(bufs->interlaced);
    heap_caps_free(bufs->col_dirtyness);
    free(bufs->expected_interlaced);
    free(bufs->expected_col_dirtyness);
}

TEST_CASE("simple aligned diff works", "[epdiy,unit]") {
    // length of the example buffers in bytes (i.e., half the length in pixels)
    const int example_len = DEFAULT_EXAMPLE_LEN;
    DiffTestBuffers bufs;
    bool dirty;

    diff_test_buffers_init(&bufs, example_len);

    // This should trigger use of vector extensions on the S3
    TEST_ASSERT((uint32_t)bufs.to % 16 == 0)

    // fully aligned
    dirty = _epd_interlace_line(
        bufs.to, bufs.from, bufs.interlaced, bufs.col_dirtyness, 2 * example_len
    );

    TEST_ASSERT(dirty == true);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bufs.expected_col_dirtyness, bufs.col_dirtyness, example_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bufs.expected_interlaced, bufs.interlaced, 2 * example_len);

    diff_test_buffers_free(&bufs);
}

TEST_CASE("dirtynes for diff without changes is correct", "[epdiy,unit]") {
    const int example_len = DEFAULT_EXAMPLE_LEN;
    const uint8_t NULL_ARRAY[DEFAULT_EXAMPLE_LEN * 2] = {0};
    DiffTestBuffers bufs;
    bool dirty;

    diff_test_buffers_init(&bufs, example_len);

    // This should trigger use of vector extensions on the S3
    TEST_ASSERT((uint32_t)bufs.to % 16 == 0)

    // both use "from" buffer
    dirty = _epd_interlace_line(
        bufs.from, bufs.from, bufs.interlaced, bufs.col_dirtyness, 2 * example_len
    );

    TEST_ASSERT(dirty == false);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(NULL_ARRAY, bufs.col_dirtyness, example_len);

    // both use "to" buffer, misaligned by 4 bytes
    dirty = _epd_interlace_line(
        bufs.to + 4, bufs.to + 4, bufs.interlaced, bufs.col_dirtyness, 2 * (example_len - 4)
    );

    TEST_ASSERT(dirty == false);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(NULL_ARRAY, bufs.col_dirtyness + 4, example_len - 4);

    diff_test_buffers_free(&bufs);
}

TEST_CASE("different 4-byte alignments work", "[epdiy,unit]") {
    const int example_len = DEFAULT_EXAMPLE_LEN;
    const uint8_t NULL_ARRAY[DEFAULT_EXAMPLE_LEN * 2] = {0};
    DiffTestBuffers bufs;
    bool dirty;

    diff_test_buffers_init(&bufs, example_len);

    // test all combinations of start / end missalignment
    for (int start_offset = 0; start_offset <= 16; start_offset += 4) {
        for (int end_offset = 0; end_offset <= 16; end_offset += 4) {
            int unaligned_len = example_len - end_offset - start_offset;

            diff_test_buffers_fill(&bufs, example_len);

            // before and after the designated range the buffer shoulld be clear
            memset(bufs.expected_col_dirtyness, 0, start_offset);
            memset(bufs.expected_interlaced, 0, 2 * start_offset);
            memset(bufs.expected_col_dirtyness + start_offset + unaligned_len, 0, end_offset);
            memset(
                bufs.expected_interlaced + (start_offset + unaligned_len) * 2, 0, end_offset * 2
            );

            printf(
                "testing  with alignment (in px): (%d, %d)... ", 2 * start_offset, 2 * unaligned_len
            );
            uint64_t start = esp_timer_get_time();

            for (int i = 0; i < 100; i++) {
                dirty = _epd_interlace_line(
                    bufs.to + start_offset, bufs.from + start_offset,
                    bufs.interlaced + 2 * start_offset, bufs.col_dirtyness + start_offset,
                    2 * unaligned_len
                );
            }

            uint64_t end = esp_timer_get_time();

            printf("took %.2fus per iter.\n", (end - start) / 100.0);



            TEST_ASSERT(dirty == true);

            TEST_ASSERT_EQUAL_UINT8_ARRAY(
                bufs.expected_col_dirtyness, bufs.col_dirtyness, example_len
            );
            TEST_ASSERT_EQUAL_UINT8_ARRAY(bufs.expected_interlaced, bufs.interlaced, example_len);
        }
    }

    diff_test_buffers_free(&bufs);
}
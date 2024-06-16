#include <esp_heap_caps.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unity.h>
#include "epd_internals.h"
#include "epdiy.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "output_common/lut.h"

#define DEFAULT_EXAMPLE_LEN 1408

static const uint8_t input_data_pattern[16] = { 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0x00, 0x01, 0x10,
                                                0xA5, 0xA5, 0x5A, 0x5A, 0xFF, 0xFF, 0x00, 0x08 };
static const uint8_t result_data_pattern_lcd_1ppB[4] = { 0x20, 0x90, 0x5A, 0x40 };
static const uint8_t result_data_pattern_lcd_2ppB_white[8]
    = { 0x00, 0x01, 0x50, 0x55, 0x55, 0x55, 0x00, 0x55 };
static const uint8_t result_data_pattern_lcd_2ppB_black[8]
    = { 0xAA, 0xA8, 0x0A, 0x82, 0xAA, 0xAA, 0xAA, 0x20 };

typedef void (*lut_func_t)(const uint32_t*, uint8_t*, const uint8_t*, uint32_t);
static uint8_t waveform_phases[16][4];

void calc_epd_input_1ppB_1k_S3_VE(
    const uint32_t* ld, uint8_t* epd_input, const uint8_t* conversion_lut, uint32_t epd_width
);
void calc_epd_input_1ppB_64k(
    const uint32_t* ld, uint8_t* epd_input, const uint8_t* conversion_lut, uint32_t epd_width
);

static EpdWaveformPhases test_waveform = {
    .phase_times = NULL,
    .phases = 1,
    .luts = (uint8_t*)waveform_phases,
};

typedef struct {
    uint8_t* line_data;
    uint8_t* result_line;
    uint8_t* expected_line;
    uint8_t* lut;
    /// Ratio of input bytes to output bytes
    int in_out_ratio;
    int example_len_px;
} LutTestBuffers;

static void fill_test_waveform() {
    for (int to = 0; to < 16; to++) {
        memset(waveform_phases[to], 0, 4);

        for (int from = 0; from < 16; from++) {
            uint8_t val = 0x00;
            if (to < from)
                val = 0x01;
            if (to > from)
                val = 0x02;
            waveform_phases[to][from >> 2] |= val << (3 - (from & 0x3)) * 2;
        }
    }
}

/**
 * (Re-)fill buffers with example data, clear result buffers.
 */
static void lut_test_buffers_fill(LutTestBuffers* bufs, const uint8_t* result_pattern) {
    int result_pattern_len = sizeof(input_data_pattern) / bufs->in_out_ratio;

    // initialize test and check patterns
    for (int i = 0; i < bufs->example_len_px; i++) {
        bufs->line_data[i] = input_data_pattern[i % sizeof(input_data_pattern)];
        bufs->expected_line[i / bufs->in_out_ratio]
            = result_pattern[(i / bufs->in_out_ratio) % result_pattern_len];
    }

    memset(bufs->lut, 0, 1 << 16);
    memset(bufs->result_line, 0, bufs->example_len_px / bufs->in_out_ratio);

    fill_test_waveform();
    heap_caps_check_integrity_all(true);
}

/*
 * Allocates and populates buffers for LUT tests.
 */
static void lut_test_buffers_init(
    LutTestBuffers* bufs, int example_len_px, const uint8_t* result_pattern, int in_out_ratio
) {
    bufs->line_data = heap_caps_aligned_alloc(16, example_len_px, MALLOC_CAP_INTERNAL);
    bufs->result_line
        = heap_caps_aligned_alloc(16, example_len_px / in_out_ratio, MALLOC_CAP_INTERNAL);
    bufs->expected_line
        = heap_caps_aligned_alloc(16, example_len_px / in_out_ratio, MALLOC_CAP_INTERNAL);
    bufs->lut = heap_caps_malloc(1 << 16, MALLOC_CAP_INTERNAL);
    bufs->example_len_px = example_len_px;
    bufs->in_out_ratio = in_out_ratio;

    lut_test_buffers_fill(bufs, result_pattern);
}

/**
 * Free buffers used for LUT testing.
 */
static void diff_test_buffers_free(LutTestBuffers* bufs) {
    heap_caps_free(bufs->line_data);
    heap_caps_free(bufs->expected_line);
    heap_caps_free(bufs->result_line);
    heap_caps_free(bufs->lut);
}

static void IRAM_ATTR test_with_alignments(LutTestBuffers* bufs, lut_func_t lut_func) {
    int len = bufs->example_len_px;
    int out_len = bufs->example_len_px / bufs->in_out_ratio;

    uint8_t* expectation_backup = heap_caps_aligned_alloc(16, out_len, MALLOC_CAP_DEFAULT);
    memcpy(expectation_backup, bufs->expected_line, out_len);

    // test combinations of start / end missalignment in four byte steps
    for (int start_offset = 0; start_offset <= 16; start_offset += 4) {
        for (int end_offset = 0; end_offset <= 16; end_offset += 4) {
            int unaligned_len = len - end_offset - start_offset;

            memset(bufs->result_line, 0, out_len);
            memcpy(bufs->expected_line, expectation_backup, out_len);

            // before and after the designated range the buffer shoulld be clear
            memset(bufs->expected_line, 0, start_offset / 4);
            memset(bufs->expected_line + (start_offset + unaligned_len) / 4, 0, end_offset / 4);

            printf(
                "benchmarking and checking with alignment (in px): (%d, %d)... ",
                start_offset,
                unaligned_len
            );
            uint64_t start = esp_timer_get_time();
            for (int i = 0; i < 100; i++) {
                lut_func(
                    (uint32_t*)(bufs->line_data + start_offset * bufs->in_out_ratio / 4),
                    bufs->result_line + start_offset / 4,
                    bufs->lut,
                    unaligned_len
                );
            }
            heap_caps_check_integrity_all(true);
            uint64_t end = esp_timer_get_time();
            printf("took %.2fus per iter.\n", (end - start) / 100.0);

            // Compare computed outputs to the expectation. We limit the comparison to len / 4,
            // since the LUT functions only compute a full display line, not more, even though our
            // test buffer may be larger.
            TEST_ASSERT_EQUAL_UINT8_ARRAY(bufs->expected_line, bufs->result_line, len / 4);
        }
    }
    heap_caps_check_integrity_all(true);

    heap_caps_free(expectation_backup);
}

TEST_CASE("1ppB lookup LCD, 64k LUT", "[epdiy,unit,lut]") {
    LutTestBuffers bufs;
    lut_test_buffers_init(&bufs, DEFAULT_EXAMPLE_LEN, result_data_pattern_lcd_1ppB, 4);

    enum EpdDrawMode mode = MODE_GL16 | MODE_PACKING_1PPB_DIFFERENCE | MODE_FORCE_NO_PIE;
    LutFunctionPair func_pair = find_lut_functions(mode, 1 << 16);
    func_pair.build_func(bufs.lut, &test_waveform, 0);
    test_with_alignments(&bufs, calc_epd_input_1ppB_64k);

    diff_test_buffers_free(&bufs);
}

TEST_CASE("1ppB lookup LCD, 1k LUT, PIE", "[epdiy,unit,lut]") {
    LutTestBuffers bufs;
    lut_test_buffers_init(&bufs, DEFAULT_EXAMPLE_LEN, result_data_pattern_lcd_1ppB, 4);

    enum EpdDrawMode mode = MODE_GL16 | MODE_PACKING_1PPB_DIFFERENCE;
    LutFunctionPair func_pair = find_lut_functions(mode, 1 << 10);
    func_pair.build_func(bufs.lut, &test_waveform, 0);
    test_with_alignments(&bufs, calc_epd_input_1ppB_1k_S3_VE);
    diff_test_buffers_free(&bufs);
}

TEST_CASE("2ppB lookup LCD, 64k LUT, previously white", "[epdiy,unit,lut]") {
    LutTestBuffers bufs;
    lut_test_buffers_init(&bufs, DEFAULT_EXAMPLE_LEN, result_data_pattern_lcd_2ppB_white, 2);

    enum EpdDrawMode mode = MODE_GL16 | MODE_PACKING_2PPB | PREVIOUSLY_WHITE;
    LutFunctionPair func_pair = find_lut_functions(mode, 1 << 16);
    func_pair.build_func(bufs.lut, &test_waveform, 0);
    test_with_alignments(&bufs, func_pair.lookup_func);

    diff_test_buffers_free(&bufs);
}

TEST_CASE("2ppB lookup LCD, 64k LUT, previously black", "[epdiy,unit,lut]") {
    LutTestBuffers bufs;
    lut_test_buffers_init(&bufs, DEFAULT_EXAMPLE_LEN, result_data_pattern_lcd_2ppB_black, 2);

    enum EpdDrawMode mode = MODE_GL16 | MODE_PACKING_2PPB | PREVIOUSLY_BLACK;
    LutFunctionPair func_pair = find_lut_functions(mode, 1 << 16);
    func_pair.build_func(bufs.lut, &test_waveform, 0);
    test_with_alignments(&bufs, func_pair.lookup_func);

    diff_test_buffers_free(&bufs);
}

TEST_CASE("2ppB lookup LCD, 1k LUT, previously white", "[epdiy,unit,lut]") {
    LutTestBuffers bufs;
    lut_test_buffers_init(&bufs, DEFAULT_EXAMPLE_LEN, result_data_pattern_lcd_2ppB_white, 2);

    enum EpdDrawMode mode = MODE_GL16 | MODE_PACKING_2PPB | PREVIOUSLY_WHITE;
    LutFunctionPair func_pair = find_lut_functions(mode, 1 << 10);
    func_pair.build_func(bufs.lut, &test_waveform, 0);
    test_with_alignments(&bufs, func_pair.lookup_func);

    diff_test_buffers_free(&bufs);
}

TEST_CASE("2ppB lookup LCD, 1k LUT, previously black", "[epdiy,unit,lut]") {
    LutTestBuffers bufs;
    lut_test_buffers_init(&bufs, DEFAULT_EXAMPLE_LEN, result_data_pattern_lcd_2ppB_black, 2);

    enum EpdDrawMode mode = MODE_GL16 | MODE_PACKING_2PPB | PREVIOUSLY_BLACK;
    LutFunctionPair func_pair = find_lut_functions(mode, 1 << 10);
    func_pair.build_func(bufs.lut, &test_waveform, 0);
    test_with_alignments(&bufs, func_pair.lookup_func);

    diff_test_buffers_free(&bufs);
}

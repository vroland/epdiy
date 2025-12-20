/**
 * High-level API implementation for epdiy.
 */

#include <assert.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_types.h>
#include <string.h>

#include "epd_highlevel.h"
#include "epdiy.h"

#ifndef _swap_int
#define _swap_int(a, b) \
    {                   \
        int t = a;      \
        a = b;          \
        b = t;          \
    }
#endif

static bool already_initialized = 0;

EpdiyHighlevelState epd_hl_init(const EpdWaveform* waveform) {
    assert(!already_initialized);
    if (waveform == NULL) {
        waveform = epd_get_display()->default_waveform;
    }

    int fb_size = epd_width() / 2 * epd_height();

#if !(defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S3_SPIRAM_SUPPORT))
    ESP_LOGW(
        "EPDiy", "Please enable PSRAM for the ESP32 (menuconfig→ Component config→ ESP32-specific)"
    );
#endif
    EpdiyHighlevelState state;
    state.back_fb = heap_caps_aligned_alloc(16, fb_size, MALLOC_CAP_SPIRAM);
    assert(state.back_fb != NULL);
    state.front_fb = heap_caps_aligned_alloc(16, fb_size, MALLOC_CAP_SPIRAM);
    assert(state.front_fb != NULL);
    state.difference_fb = heap_caps_aligned_alloc(16, 2 * fb_size, MALLOC_CAP_SPIRAM);
    assert(state.difference_fb != NULL);
    state.dirty_lines = malloc(epd_height() * sizeof(bool));
    assert(state.dirty_lines != NULL);
    state.dirty_columns
        = heap_caps_aligned_alloc(16, epd_width() / 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(state.dirty_columns != NULL);
    state.waveform = waveform;

    memset(state.front_fb, 0xFF, fb_size);
    memset(state.back_fb, 0xFF, fb_size);
    bool is_mirrored = ((epd_get_display()->display_type & DISPLAY_TYPE_HORIZONTAL_MIRRORED) != 0); 
    printf("is_mirrored: %d\n\n", is_mirrored);
    state.mirror_x = is_mirrored;
    already_initialized = true;
    return state;
}

uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* state) {
    assert(state != NULL);
    return state->front_fb;
}

enum EpdDrawError epd_hl_update_screen(
    EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature
) {
    return epd_hl_update_area(state, mode, temperature, epd_full_screen());
}

EpdRect _inverse_rotated_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    // If partial update uses full screen do not rotate anything
    if (!(x == 0 && y == 0 && epd_width() == w && epd_height() == h)) {
        // invert the current display rotation
        switch (epd_get_rotation()) {
            // 0 landscape: Leave it as is
            case EPD_ROT_LANDSCAPE:
                break;
            // 1 90 ° clockwise
            case EPD_ROT_PORTRAIT:
                _swap_int(x, y);
                _swap_int(w, h);
                x = epd_width() - x - w;
                break;

            case EPD_ROT_INVERTED_LANDSCAPE:
                // 3 180°
                x = epd_width() - x - w;
                y = epd_height() - y - h;
                break;

            case EPD_ROT_INVERTED_PORTRAIT:
                // 3 270 °
                _swap_int(x, y);
                _swap_int(w, h);
                y = epd_height() - y - h;
                break;
        }
    }

    EpdRect rotated = { x, y, w, h };
    return rotated;
}

// Pre-computed lookup table for reversing 2 pixels within a byte
static const uint8_t __attribute__((aligned(4))) reverse_2px_lut[256] = {
    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,  // 0x00-0x07
    0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0,  // 0x08-0x0F
    0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71,  // 0x10-0x17
    0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1,  // 0x18-0x1F
    0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72,  // 0x20-0x27
    0x82, 0x92, 0xA2, 0xB2, 0xC2, 0xD2, 0xE2, 0xF2,  // 0x28-0x2F
    0x03, 0x13, 0x23, 0x33, 0x43, 0x53, 0x63, 0x73,  // 0x30-0x37
    0x83, 0x93, 0xA3, 0xB3, 0xC3, 0xD3, 0xE3, 0xF3,  // 0x38-0x3F
    0x04, 0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74,  // 0x40-0x47
    0x84, 0x94, 0xA4, 0xB4, 0xC4, 0xD4, 0xE4, 0xF4,  // 0x48-0x4F
    0x05, 0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75,  // 0x50-0x57
    0x85, 0x95, 0xA5, 0xB5, 0xC5, 0xD5, 0xE5, 0xF5,  // 0x58-0x5F
    0x06, 0x16, 0x26, 0x36, 0x46, 0x56, 0x66, 0x76,  // 0x60-0x67
    0x86, 0x96, 0xA6, 0xB6, 0xC6, 0xD6, 0xE6, 0xF6,  // 0x68-0x6F
    0x07, 0x17, 0x27, 0x37, 0x47, 0x57, 0x67, 0x77,  // 0x70-0x77
    0x87, 0x97, 0xA7, 0xB7, 0xC7, 0xD7, 0xE7, 0xF7,  // 0x78-0x7F
    0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68, 0x78,  // 0x80-0x87
    0x88, 0x98, 0xA8, 0xB8, 0xC8, 0xD8, 0xE8, 0xF8,  // 0x88-0x8F
    0x09, 0x19, 0x29, 0x39, 0x49, 0x59, 0x69, 0x79,  // 0x90-0x97
    0x89, 0x99, 0xA9, 0xB9, 0xC9, 0xD9, 0xE9, 0xF9,  // 0x98-0x9F
    0x0A, 0x1A, 0x2A, 0x3A, 0x4A, 0x5A, 0x6A, 0x7A,  // 0xA0-0xA7
    0x8A, 0x9A, 0xAA, 0xBA, 0xCA, 0xDA, 0xEA, 0xFA,  // 0xA8-0xAF
    0x0B, 0x1B, 0x2B, 0x3B, 0x4B, 0x5B, 0x6B, 0x7B,  // 0xB0-0xB7
    0x8B, 0x9B, 0xAB, 0xBB, 0xCB, 0xDB, 0xEB, 0xFB,  // 0xB8-0xBF
    0x0C, 0x1C, 0x2C, 0x3C, 0x4C, 0x5C, 0x6C, 0x7C,  // 0xC0-0xC7
    0x8C, 0x9C, 0xAC, 0xBC, 0xCC, 0xDC, 0xEC, 0xFC,  // 0xC8-0xCF
    0x0D, 0x1D, 0x2D, 0x3D, 0x4D, 0x5D, 0x6D, 0x7D,  // 0xD0-0xD7
    0x8D, 0x9D, 0xAD, 0xBD, 0xCD, 0xDD, 0xED, 0xFD,  // 0xD8-0xDF
    0x0E, 0x1E, 0x2E, 0x3E, 0x4E, 0x5E, 0x6E, 0x7E,  // 0xE0-0xE7
    0x8E, 0x9E, 0xAE, 0xBE, 0xCE, 0xDE, 0xEE, 0xFE,  // 0xE8-0xEF
    0x0F, 0x1F, 0x2F, 0x3F, 0x4F, 0x5F, 0x6F, 0x7F,  // 0xF0-0xF7
    0x8F, 0x9F, 0xAF, 0xBF, 0xCF, 0xDF, 0xEF, 0xFF   // 0xF8-0xFF
};

/**
 * Horizontally mirror a framebuffer line with 4-bit packed pixels
 *
 * Performs horizontal mirroring by reversing both byte order and pixel order
 * within each byte. Each byte contains 2 pixels in 4-bit format [P1|P0].
 * Uses loop unrolling and lookup table for optimal scalar performance.
 *
 * @param line Pointer to the line buffer to mirror in-place
 * @param line_bytes Number of bytes in the line (width_pixels / 2)
 */
__attribute__((optimize("O3"))) static inline void mirror_framebuffer_line_horizontal_scalar(
    uint8_t* line, int line_bytes
) {
    uint8_t* start = line;
    uint8_t* end = line + line_bytes - 1;

    // Process 4 byte pairs at a time (unroll by 4)
    while (start + 3 < end) {
        // Load and reverse 4 pairs of bytes
        uint8_t s0 = reverse_2px_lut[start[0]];
        uint8_t s1 = reverse_2px_lut[start[1]];
        uint8_t s2 = reverse_2px_lut[start[2]];
        uint8_t s3 = reverse_2px_lut[start[3]];

        uint8_t e0 = reverse_2px_lut[end[0]];
        uint8_t e1 = reverse_2px_lut[end[-1]];
        uint8_t e2 = reverse_2px_lut[end[-2]];
        uint8_t e3 = reverse_2px_lut[end[-3]];

        // Store swapped values
        start[0] = e0;
        start[1] = e1;
        start[2] = e2;
        start[3] = e3;

        end[0] = s0;
        end[-1] = s1;
        end[-2] = s2;
        end[-3] = s3;

        start += 4;
        end -= 4;
    }

    // Handle remaining bytes
    while (start < end) {
        uint8_t temp_start = reverse_2px_lut[*start];
        uint8_t temp_end = reverse_2px_lut[*end];

        *start = temp_end;
        *end = temp_start;

        start++;
        end--;
    }

    // Handle middle byte for odd line_bytes
    if (start == end) {
        *start = reverse_2px_lut[*start];
    }
}

/**
 * Mirror entire framebuffer horizontally
 */
void epd_hl_mirror_framebuffer_horizontal(uint8_t* framebuffer, int width, int height) {
    int line_bytes = width / 2;  // 2 pixels per byte in framebuffer

    for (int y = 0; y < height; y++) {
        uint8_t* line = framebuffer + y * line_bytes;
        mirror_framebuffer_line_horizontal_scalar(line, line_bytes);
    }
}

/**
 * Updated epd_hl_update_area with horizontal mirroring support
 */
enum EpdDrawError epd_hl_update_area(
    EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature, EpdRect area
) {
    assert(state != NULL);

    // Check if we need to apply horizontal mirroring
    bool mirror_x = state->mirror_x;

    uint32_t ts = esp_timer_get_time() / 1000;

    // Apply mirroring to framebuffers if needed
    if (mirror_x) {
        epd_hl_mirror_framebuffer_horizontal(state->front_fb, epd_width(), epd_height());
        epd_hl_mirror_framebuffer_horizontal(state->back_fb, epd_width(), epd_height());
    }

    uint32_t tm1 = esp_timer_get_time() / 1000;  // After first mirroring

    // Apply rotation transformation to area
    EpdRect rotated_area = _inverse_rotated_area(area.x, area.y, area.width, area.height);
    area.x = rotated_area.x;
    area.y = rotated_area.y;
    area.width = rotated_area.width;
    area.height = rotated_area.height;

    uint32_t tr = esp_timer_get_time() / 1000;

    // FIXME: use crop information here, if available
    EpdRect diff_area = epd_difference_image_cropped(
        state->front_fb,
        state->back_fb,
        area,
        state->difference_fb,
        state->dirty_lines,
        state->dirty_columns
    );

    if (diff_area.height == 0 || diff_area.width == 0) {
        // Restore framebuffers if they were mirrored
        if (mirror_x) {
            epd_hl_mirror_framebuffer_horizontal(state->front_fb, epd_width(), epd_height());
            epd_hl_mirror_framebuffer_horizontal(state->back_fb, epd_width(), epd_height());
        }
        return EPD_DRAW_SUCCESS;
    }

    uint32_t t1 = esp_timer_get_time() / 1000;

    diff_area.x = 0;
    diff_area.y = 0;
    diff_area.width = epd_width();
    diff_area.height = epd_height();

    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    err = epd_draw_base(
        epd_full_screen(),
        state->difference_fb,
        diff_area,
        MODE_PACKING_1PPB_DIFFERENCE | mode,
        temperature,
        state->dirty_lines,
        state->dirty_columns,
        state->waveform
    );

    uint32_t t2 = esp_timer_get_time() / 1000;

    diff_area.x = 0;
    diff_area.y = 0;
    diff_area.width = epd_width();
    diff_area.height = epd_height();

    int buf_width = epd_width();

    for (int l = diff_area.y; l < diff_area.y + diff_area.height; l++) {
        if (state->dirty_lines[l] > 0) {
            uint8_t* lfb = state->front_fb + buf_width / 2 * l;
            uint8_t* lbb = state->back_fb + buf_width / 2 * l;

            int x = diff_area.x;
            int x_last = diff_area.x + diff_area.width - 1;

            if (x % 2) {
                *(lbb + x / 2) = (*(lfb + x / 2) & 0xF0) | (*(lbb + x / 2) & 0x0F);
                x += 1;
            }

            if (!(x_last % 2)) {
                *(lbb + x_last / 2) = (*(lfb + x_last / 2) & 0x0F) | (*(lbb + x_last / 2) & 0xF0);
                x_last -= 1;
            }

            memcpy(lbb + (x / 2), lfb + (x / 2), (x_last - x + 1) / 2);
        }
    }

    uint32_t tm2_start = esp_timer_get_time() / 1000;

    // Restore framebuffers if they were mirrored (second mirroring)
    if (mirror_x) {
        epd_hl_mirror_framebuffer_horizontal(state->front_fb, epd_width(), epd_height());
        epd_hl_mirror_framebuffer_horizontal(state->back_fb, epd_width(), epd_height());
    }

    uint32_t tm2_end = esp_timer_get_time() / 1000;

    ESP_LOGI(
        "epdiy",
        "mirror: %dms, rot: %dms, diff: %dms, draw: %dms, buffer update: %dms, mirror_rev: %dms, "
        "total: %dms",
        tm1 - ts,             // First mirroring time
        tr - tm1,             // Rotation time
        t1 - tr,              // Difference calculation time
        t2 - t1,              // Drawing time
        tm2_start - t2,       // Buffer update time
        tm2_end - tm2_start,  // Second mirroring time
        tm2_end - ts          // Total time
    );

    return err;
}

void epd_hl_set_all_white(EpdiyHighlevelState* state) {
    assert(state != NULL);
    int fb_size = epd_width() / 2 * epd_height();
    memset(state->front_fb, 0xFF, fb_size);
}

void epd_fullclear(EpdiyHighlevelState* state, int temperature) {
    assert(state != NULL);
    epd_hl_set_all_white(state);
    enum EpdDrawError err = epd_hl_update_screen(state, MODE_GC16, temperature);
    assert(err == EPD_DRAW_SUCCESS);
    epd_clear();
}

void epd_hl_waveform(EpdiyHighlevelState* state, const EpdWaveform* waveform) {
    if (waveform == NULL) {
        waveform = epd_get_display()->default_waveform;
    }
    state->waveform = waveform;
}
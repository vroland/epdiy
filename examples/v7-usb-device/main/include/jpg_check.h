#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

// Return true if buffer looks like a baseline (SOF0) JPEG, dimensions within limits.
// Rejects progressive (SOF2) and obviously corrupted streams.
static bool jpg_quick_check_buf(const uint8_t *buf, size_t len, int *out_w, int *out_h, int max_w = 4096, int max_h = 4096)
{
    if (!buf || len < 4) return false;
    size_t pos = 0;
    // SOI
    if (buf[pos] != 0xFF || buf[pos+1] != 0xD8) return false;
    pos += 2;
    while (pos + 4 < len) {
        if (buf[pos] != 0xFF) {
            // skip until marker
            pos++;
            continue;
        }
        // read marker
        uint8_t marker = buf[pos+1];
        pos += 2;
        // Standalone markers: 0xD0..0xD9 have no length. SOS (0xDA) is followed by compressed data
        if (marker == 0xD9 || marker == 0xDA) break;
        if (pos + 2 > len) return false;
        uint16_t length = (buf[pos] << 8) | buf[pos+1];
        if (length < 2) return false;
        pos += 2;
        if (pos + (length - 2) > len) return false;
        // SOF0 = 0xC0 (baseline), SOF2 = 0xC2 (progressive)
        if (marker == 0xC0 || marker == 0xC2) {
            if (length < 7) return false;
            uint8_t precision = buf[pos];
            uint16_t height = (buf[pos+1] << 8) | buf[pos+2];
            uint16_t width  = (buf[pos+3] << 8) | buf[pos+4];
            if (width == 0 || height == 0) return false;
            if (width > (uint32_t)max_w || height > (uint32_t)max_h) return false;
            if (marker == 0xC2) return false; // progressive — reject if decoder can't do it
            if (out_w) *out_w = width;
            if (out_h) *out_h = height;
            return true;
        }
        pos += (length - 2);
    }
    return false;
}
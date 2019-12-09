#include "font.h"

#include "EPD.h"

#include "zlib.h"
#include <math.h>


/*!
   @brief   Draw a single character to a pre-allocated buffer
*/
void drawChar(GFXfont* font, uint8_t* buffer, int* cursor_x, uint16_t buf_width, uint16_t buf_height, int16_t baseline_height, uint8_t c) {

    // filter out invalid character ranges
    if (c > 0xA0) {
        c -= 0x21;
    }
    c -= font->first;

    GFXglyph *glyph  = &font->glyph[c];

    uint32_t offset = glyph->data_offset;
    uint8_t  width  = glyph->width,
             height  = glyph->height;
    int   left = glyph->left;

    unsigned long bitmap_size = width * height;
    uint8_t* bitmap = (uint8_t*)malloc(bitmap_size);
    uncompress(bitmap, &bitmap_size, &font->bitmap[offset], glyph->compressed_size);
    for (uint32_t i=0; i<bitmap_size; i++) {
        int xx = *cursor_x + left + i % width;
        int yy = buf_height - (glyph->top + baseline_height - i / width);
        buffer[yy * buf_width + xx] = bitmap[i];
    }
    free(bitmap);
    *cursor_x += glyph->advance_x;
}

/*!
 * @brief Calculate the bounds of a character when drawn at (x, y), move the cursor (*x) forward, adjust the given bounds.
 */
void getCharBounds(GFXfont* font, unsigned char c, int* x, int* y, int* minx, int* miny, int* maxx, int* maxy) {
    if (c != '\r' && c != '\n') {
        // filter out invalid character ranges
        if (c > 0xA0) {
            c -= 0x21;
        }
        c -= font->first;

        GFXglyph *glyph  = &font->glyph[c];
        int x1 = *x + glyph->left,
            y1 = *y + (glyph->top - glyph->height),
            x2 = x1 + glyph->width,
            y2 = y1 + glyph->height;
        if(x1 < *minx) *minx = x1;
        if(y1 < *miny) *miny = y1;
        if(x2 > *maxx) *maxx = x2;
        if(y2 > *maxy) *maxy = y2;
        *x += glyph->advance_x;
    }
}

int min(int x, int y) {
    return x < y ? x : y;
}

void getTextBounds(GFXfont* font, unsigned char* string, int x, int y, int* x1, int* y1, int* w, int* h) {
    int xx = x, yy = y, minx = 100000, miny = 100000, maxx = -1, maxy = -1;
    unsigned char c;
    while ((c=*(string++))) {
        getCharBounds(font, c, &xx, &yy, &minx, &miny, &maxx, &maxy);
    }
    *x1 = min(x, minx);
    *w = maxx - *x1;
    *y1 = miny;
    *h = maxy - miny;
}

void writeln(GFXfont* font, unsigned char* string, int* cursor_x, int* cursor_y) {

    int x1 = 0, y1 = 0, w = 0, h = 0;
    getTextBounds(font, string, *cursor_x, *cursor_y, &x1, &y1, &w, &h);
    uint8_t* buffer = (uint8_t*)malloc(w * h);
    memset(buffer, 255, w * h);
    unsigned char c;
    int baseline_height = *cursor_y - y1;

    Rect_t area = {
        .x = x1,
        .y = *cursor_y - h + baseline_height,
        .width = w,
        .height = h
    };

    int working_curor = 0;
    while ((c=*(string++))) {
        drawChar(font, buffer, &working_curor, w, h, (*cursor_y - y1), c);
    }
    volatile uint32_t t = micros();
    epd_draw_picture(area, buffer);
    volatile uint32_t t2 = micros();
    printf("drawing took %d us.\n", t2 - t);
    free(buffer);
}

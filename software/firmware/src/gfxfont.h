// Font structures adapted from Adafruit_GFX (1.1 and later).

#ifndef _GFXFONT_H_
#define _GFXFONT_H_

/// Font data stored PER GLYPH
typedef struct {
    uint8_t  width;            ///< Bitmap dimensions in pixels
    uint8_t  height;           ///< Bitmap dimensions in pixels
    uint8_t  advance_x;        ///< Distance to advance cursor (x axis)
    int16_t   left;             ///< X dist from cursor pos to UL corner
    int16_t   top;              ///< Y dist from cursor pos to UL corner
    uint16_t compressed_size;  ///< Size of the zlib-compressed font data.
    uint32_t data_offset;      ///< Pointer into GFXfont->bitmap
} GFXglyph;

/// Data stored for FONT AS A WHOLE
typedef struct {
    uint8_t  *bitmap;      ///< Glyph bitmaps, concatenated
    GFXglyph *glyph;       ///< Glyph array
    uint8_t   first;       ///< ASCII extents (first char)
    uint8_t   last;        ///< ASCII extents (last char)
    uint8_t   advance_y;   ///< Newline distance (y axis)
} GFXfont;

// Draw a character
/**************************************************************************/
/*!
   @brief   Draw a single character
    @param    x   Bottom left corner x coordinate
    @param    y   Bottom left corner y coordinate
    @param    c   The 8-bit font-indexed character (likely ascii)
    @param    size_x  Font magnification level in X-axis, 1 is 'original' size
    @param    size_y  Font magnification level in Y-axis, 1 is 'original' size
*/
/**************************************************************************/
void drawChar(GFXfont* font, uint8_t* buffer, int* cursor_x, uint16_t buf_width, uint16_t buf_height, int16_t baseline_height, uint8_t c) {

    Serial.print("draw: ");
    Serial.println((char)c);
    Serial.print("bl height: ");
    Serial.println(baseline_height);
    if (c > 0xA0) {
        c -= 0x21;
    }
    c -= font->first;
    GFXglyph *glyph  = &font->glyph[c];

    uint32_t offset = glyph->data_offset;
    uint8_t  width  = glyph->width,
             height  = glyph->height;
    int   left = glyph->left,
           top_offset = 1 - glyph->top;

    // Todo: Add character clipping here

    uint16_t bitmap_width = width + width % 2;
    unsigned long bitmap_size = bitmap_width * height;
    uint8_t* bitmap = (uint8_t*)malloc(bitmap_size);
    uncompress(bitmap, &bitmap_size, &font->bitmap[offset], glyph->compressed_size);
    for (uint32_t i=0; i<bitmap_size / 2; i++) {
        uint16_t xx = *cursor_x + left + i % bitmap_width;
        uint16_t yy = buf_height - baseline_height + top_offset + i / height;
        buffer[yy * buf_width + xx] = bitmap[i];
    }
    free(bitmap);
    *cursor_x += glyph->advance_x;
}

void getCharBounds(GFXfont* font, unsigned char c, int* x, int* y, int* minx, int* miny, int* maxx, int* maxy) {
    if (c != '\r' && c != '\n') {
        // skip undefined latin1 range
        if (c > 0xA0) {
            c -= 0x21;
        }
        c -= font->first;
        GFXglyph *glyph  = &font->glyph[c];
        int x1 = *x + glyph->left,
            y1 = *y + glyph->top,
            x2 = x1 + glyph->width,
            y2 = y1 + glyph->height - 1;
        if(x1 < *minx) *minx = x1;
        if(y1 < *miny) *miny = y1;
        if(x2 > *maxx) *maxx = x2;
        if(y2 > *maxy) *maxy = y2;
        *x += glyph->advance_x;
    }
}

void getTextBounds(GFXfont* font, unsigned char* string, int x, int y, int* x1, int* y1, int* w, int* h) {
    int xx = x, yy = y, minx = 100000, miny = 100000, maxx = -1, maxy = -1;
    unsigned char c;
    while (c=*(string++)) {
        getCharBounds(font, c, &xx, &yy, &minx, &miny, &maxx, &maxy);
    }
    *x1 = minx;
    *w = maxx - minx + 1;
    *y1 = miny;
    *h = maxy - miny + 1;
}


void write(GFXfont* font, unsigned char* string, int* cursor_x, int* cursor_y, EPD* epd) {

    int x1 = 0, y1 = 0, w = 0, h = 0;
    getTextBounds(font, string, *cursor_x, *cursor_y, &x1, &y1, &w, &h);
    uint8_t* buffer = (uint8_t*)malloc(w * h);
    unsigned char c;
    Serial.print("Bounds: ");
    Serial.print(x1);
    Serial.print(" ");
    Serial.print(y1);
    Serial.print(" ");
    Serial.print(w);
    Serial.print(" ");
    Serial.println(h);

    Rect_t area = {
        .x = *cursor_x + (x1 - *cursor_x),
        .y = *cursor_y + (y1 - *cursor_y),
        .width = w,
        .height = h
    };
    int working_curor = 0;
    while (c=*(string++)) {
        Serial.print("draw: ");
        Serial.println((char)c);
        drawChar(font, buffer, &working_curor, w, h, (y1 - *cursor_y), c);
    }
    epd->draw_picture(area, buffer);
    free(buffer);
}
#endif // _GFXFONT_H_

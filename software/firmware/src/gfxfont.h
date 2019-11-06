// Font structures adapted from Adafruit_GFX (1.1 and later).

#ifndef _GFXFONT_H_
#define _GFXFONT_H_

/// Font data stored PER GLYPH
typedef struct {
    uint8_t  width;            ///< Bitmap dimensions in pixels
    uint8_t  height;           ///< Bitmap dimensions in pixels
    uint8_t  advance_x;        ///< Distance to advance cursor (x axis)
    int8_t   left;             ///< X dist from cursor pos to UL corner
    int8_t   top;              ///< Y dist from cursor pos to UL corner
    uint16_t compressed_size;  ///< Size of the zlib-compressed font data.
    uint32_t bitmapOffset;     ///< Pointer into GFXfont->bitmap
} GFXglyph;

/// Data stored for FONT AS A WHOLE
typedef struct {
    uint8_t  *bitmap;      ///< Glyph bitmaps, concatenated
    GFXglyph *glyph;       ///< Glyph array
    uint8_t   first;       ///< ASCII extents (first char)
    uint8_t   last;        ///< ASCII extents (last char)
    uint8_t   advance_y;   ///< Newline distance (y axis)
} GFXfont;

#endif // _GFXFONT_H_

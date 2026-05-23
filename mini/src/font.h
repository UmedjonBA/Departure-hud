#pragma once
#include "draw.h"
#include <stdbool.h>

typedef struct font font_t;

/* Load a TTF/OTF face at the given pixel size. NULL on failure. */
font_t *font_open(const char *path, int pixel_size);
void    font_close(font_t *f);

/* Distance (in pixels) from one baseline to the next. */
int     font_line_height(const font_t *f);
/* Distance from origin to top of typical glyph (positive). */
int     font_ascent     (const font_t *f);
/* Advance width of a monospace cell (we treat all glyphs as monospace; uses
 * the advance of 'M' if present, else the face's max advance). */
int     font_cell_width (const font_t *f);

/* Width in pixels of `text`, equal to cell_width * strlen(text). UTF-8 is
 * decoded but treated as one cell per codepoint, which is fine for our
 * ASCII/box-drawing labels. */
int     font_text_width (const font_t *f, const char *text);

/* Draw `text` starting at (x, y) where y is the BASELINE. */
void    font_draw       (fb_t *fb, const font_t *f, int x, int baseline,
                         const char *text, uint32_t fg);

#include "font.h"
#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Cache one rendered glyph (8-bit alpha bitmap, top-left aligned). */
typedef struct {
    uint8_t *bitmap;   /* width*rows bytes, may be NULL for blank glyphs */
    int      width;
    int      rows;
    int      pitch;    /* row stride in bytes */
    int      bearing_x;
    int      bearing_y;
    int      advance;  /* pixels */
    int      codepoint;
    bool     valid;
} glyph_t;

struct font {
    FT_Library lib;
    FT_Face    face;
    int        pixel_size;
    int        cell_w;
    int        ascent;
    int        line_h;
    /* Direct-mapped cache for printable ASCII (0x20–0x7E). Larger codepoints
     * fall through to an on-demand small array. */
    glyph_t    ascii[128];
    /* For our use case (Departure Mono + ASCII + a handful of box-drawing
     * glyphs) this stays under a few dozen entries. */
    glyph_t   *extras;
    int        extras_n;
    int        extras_cap;
};

static FT_Library g_lib = NULL;
static int        g_lib_refs = 0;

static FT_Library acquire_lib(void) {
    if (!g_lib) {
        if (FT_Init_FreeType(&g_lib) != 0) return NULL;
    }
    g_lib_refs++;
    return g_lib;
}

static void release_lib(void) {
    if (--g_lib_refs <= 0 && g_lib) {
        FT_Done_FreeType(g_lib);
        g_lib = NULL;
        g_lib_refs = 0;
    }
}

static bool rasterize(font_t *f, int codepoint, glyph_t *out) {
    FT_UInt gi = FT_Get_Char_Index(f->face, codepoint);
    if (FT_Load_Glyph(f->face, gi, FT_LOAD_DEFAULT) != 0) return false;
    if (FT_Render_Glyph(f->face->glyph, FT_RENDER_MODE_NORMAL) != 0) return false;

    FT_Bitmap *bm = &f->face->glyph->bitmap;
    out->width     = bm->width;
    out->rows      = bm->rows;
    out->pitch     = bm->pitch;
    out->bearing_x = f->face->glyph->bitmap_left;
    out->bearing_y = f->face->glyph->bitmap_top;
    out->advance   = f->face->glyph->advance.x >> 6;
    out->codepoint = codepoint;
    out->valid     = true;

    if (bm->width > 0 && bm->rows > 0) {
        size_t sz = (size_t)bm->pitch * bm->rows;
        out->bitmap = malloc(sz);
        if (!out->bitmap) return false;
        memcpy(out->bitmap, bm->buffer, sz);
    } else {
        out->bitmap = NULL;
    }
    return true;
}

font_t *font_open(const char *path, int pixel_size) {
    FT_Library lib = acquire_lib();
    if (!lib) return NULL;

    font_t *f = calloc(1, sizeof(*f));
    if (!f) { release_lib(); return NULL; }
    f->lib = lib;
    f->pixel_size = pixel_size;

    if (FT_New_Face(lib, path, 0, &f->face) != 0) {
        free(f); release_lib(); return NULL;
    }
    if (FT_Set_Pixel_Sizes(f->face, 0, pixel_size) != 0) {
        FT_Done_Face(f->face); free(f); release_lib(); return NULL;
    }

    f->ascent = f->face->size->metrics.ascender >> 6;
    f->line_h = f->face->size->metrics.height   >> 6;

    /* Prime the ASCII cache so first-frame draw is allocation-free. */
    for (int c = 0x20; c < 0x7F; c++) {
        rasterize(f, c, &f->ascii[c]);
    }
    /* Cell width: advance of 'M' for monospace; fall back to space. */
    if      (f->ascii['M'].valid && f->ascii['M'].advance) f->cell_w = f->ascii['M'].advance;
    else if (f->ascii[' '].valid && f->ascii[' '].advance) f->cell_w = f->ascii[' '].advance;
    else                                                   f->cell_w = pixel_size / 2;

    return f;
}

void font_close(font_t *f) {
    if (!f) return;
    for (int c = 0; c < 128; c++) free(f->ascii[c].bitmap);
    for (int i = 0; i < f->extras_n; i++) free(f->extras[i].bitmap);
    free(f->extras);
    FT_Done_Face(f->face);
    free(f);
    release_lib();
}

int font_line_height(const font_t *f) { return f->line_h; }
int font_ascent     (const font_t *f) { return f->ascent; }
int font_cell_width (const font_t *f) { return f->cell_w; }

/* Tiny UTF-8 decoder: returns codepoint and advances *p. Returns -1 on EOS. */
static int utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    if (!*s) return -1;
    int c;
    if      (s[0] < 0x80) { c = s[0]; *p += 1; }
    else if ((s[0] & 0xE0) == 0xC0 && s[1])
                          { c = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); *p += 2; }
    else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2])
                          { c = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3; }
    else if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3])
                          { c = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                                ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); *p += 4; }
    else                  { c = '?'; *p += 1; }
    return c;
}

static glyph_t *glyph_for(font_t *f, int cp) {
    if (cp >= 0x20 && cp < 0x7F && f->ascii[cp].valid) return &f->ascii[cp];
    /* Linear scan over extras; we expect just a handful (·, °, ↑, ↓, ─, etc.). */
    for (int i = 0; i < f->extras_n; i++) {
        if (f->extras[i].codepoint == cp) return &f->extras[i];
    }
    if (f->extras_n >= f->extras_cap) {
        int newcap = f->extras_cap ? f->extras_cap * 2 : 16;
        glyph_t *grown = realloc(f->extras, newcap * sizeof(glyph_t));
        if (!grown) return NULL;
        f->extras = grown; f->extras_cap = newcap;
    }
    glyph_t *g = &f->extras[f->extras_n];
    memset(g, 0, sizeof(*g));
    if (!rasterize(f, cp, g)) return NULL;
    f->extras_n++;
    return g;
}

int font_text_width(const font_t *f, const char *text) {
    int n = 0;
    const char *p = text;
    while (utf8_next(&p) >= 0) n++;
    return n * f->cell_w;
}

void font_draw(fb_t *fb, const font_t *f, int x, int baseline,
               const char *text, uint32_t fg) {
    /* We render text as monospace cells: each glyph is placed in a fixed-width
     * slot starting at x, x + cell_w, x + 2*cell_w… This is what Departure Mono
     * and our QML layout assume. */
    const char *p = text;
    int cell_x = x;
    int cp;
    while ((cp = utf8_next(&p)) >= 0) {
        glyph_t *g = glyph_for((font_t *)f, cp);
        if (g && g->bitmap) {
            /* Horizontally center the glyph inside its cell so narrow chars
             * (':', '.') don't stick to the left edge. */
            int gx = cell_x + (f->cell_w - g->width) / 2;
            int gy = baseline - g->bearing_y;
            draw_alpha_mask(fb, gx, gy, g->bitmap, g->width, g->rows, g->pitch, fg);
        }
        cell_x += f->cell_w;
    }
}

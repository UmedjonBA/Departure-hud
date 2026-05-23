#pragma once
#include <stdint.h>
#include <stddef.h>

/* 32-bit ARGB pixel buffer (Wayland WL_SHM_FORMAT_ARGB8888,
 * little-endian: bytes are [B][G][R][A]).
 */
typedef struct {
    uint32_t *px;   /* width * height pixels */
    int       w, h;
    int       stride;   /* in pixels */
} fb_t;

static inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g <<  8) | ((uint32_t)b      );
}

/* Solid background fill. */
void draw_clear(fb_t *fb, uint32_t color);

/* Filled axis-aligned rectangle, clipped to fb bounds. */
void draw_rect(fb_t *fb, int x, int y, int w, int h, uint32_t color);

/* One-pixel thick lines. */
void draw_hline(fb_t *fb, int x, int y, int w, uint32_t color);
void draw_vline(fb_t *fb, int x, int y, int h, uint32_t color);
void draw_line (fb_t *fb, int x0, int y0, int x1, int y1, uint32_t color);

/* Stroked rectangle outline. */
void draw_frame(fb_t *fb, int x, int y, int w, int h, uint32_t color);

/* Blit a single-channel (8 bpp) alpha mask onto fb with the given foreground
 * color. `mask` is a w*h byte array, row stride = `mask_pitch`. Pixels outside
 * fb are clipped. */
void draw_alpha_mask(fb_t *fb, int dst_x, int dst_y,
                     const uint8_t *mask, int w, int h, int mask_pitch,
                     uint32_t fg);

/* Single-pixel circle outline (midpoint algorithm). */
void draw_circle(fb_t *fb, int cx, int cy, int r, uint32_t color);

/* Ellipse outline (a, b are semi-axes). Optionally clipped to a circle of
 * radius `clip_r` around the same center — useful for sphere-wireframe
 * effects. Pass clip_r=0 to skip clipping. */
void draw_ellipse(fb_t *fb, int cx, int cy, int a, int b, int clip_r,
                  uint32_t color);

/* Arc outline from a0 to a1 radians (mathematical convention: 0 = +x axis,
 * +y is DOWN in screen coords so PI..2PI traces the lower half). */
void draw_arc(fb_t *fb, int cx, int cy, int r,
              double a0, double a1, uint32_t color);

/* Thick line, approximated as two parallel one-pixel lines. */
void draw_thick_line(fb_t *fb, int x0, int y0, int x1, int y1,
                     int thickness, uint32_t color);

/* Filled left-pointing triangle ("◂"): tip at (tip_x, mid_y), base of height
 * `h` at tip_x + w. Used for the PitchStack marker (drawn directly so we don't
 * depend on the font having U+25C2). */
void draw_tri_left(fb_t *fb, int tip_x, int mid_y, int w, int h, uint32_t color);

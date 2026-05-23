#include "draw.h"
#include <string.h>

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void draw_clear(fb_t *fb, uint32_t color) {
    /* Fast path: solid fill of every pixel. */
    int n = fb->stride * fb->h;
    for (int i = 0; i < n; i++) fb->px[i] = color;
}

void draw_rect(fb_t *fb, int x, int y, int w, int h, uint32_t color) {
    int x0 = clampi(x,         0, fb->w);
    int y0 = clampi(y,         0, fb->h);
    int x1 = clampi(x + w,     0, fb->w);
    int y1 = clampi(y + h,     0, fb->h);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = fb->px + (size_t)yy * fb->stride + x0;
        for (int xx = x0; xx < x1; xx++) *row++ = color;
    }
}

void draw_hline(fb_t *fb, int x, int y, int w, uint32_t color) {
    draw_rect(fb, x, y, w, 1, color);
}

void draw_vline(fb_t *fb, int x, int y, int h, uint32_t color) {
    draw_rect(fb, x, y, 1, h, color);
}

void draw_frame(fb_t *fb, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    draw_hline(fb, x,           y,           w, color);
    draw_hline(fb, x,           y + h - 1,   w, color);
    draw_vline(fb, x,           y,           h, color);
    draw_vline(fb, x + w - 1,   y,           h, color);
}

void draw_line(fb_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    /* Bresenham. Good enough for our axis-tickmarks; we don't need AA here. */
    int dx =  (x1 > x0 ? x1 - x0 : x0 - x1);
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if ((unsigned)x0 < (unsigned)fb->w && (unsigned)y0 < (unsigned)fb->h) {
            fb->px[(size_t)y0 * fb->stride + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Alpha-over blend: dst = src*a + dst*(1-a), 8-bit per channel.
 * We treat the framebuffer as opaque (alpha=0xFF on output) since most
 * Wayland compositors composite ARGB onto a black backdrop anyway. */
static inline uint32_t blend_over(uint32_t dst, uint32_t src, uint8_t a) {
    if (a == 0)   return dst;
    if (a == 255) return src;
    uint32_t inv = 255 - a;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    uint32_t r = (sr * a + dr * inv + 127) / 255;
    uint32_t g = (sg * a + dg * inv + 127) / 255;
    uint32_t b = (sb * a + db * inv + 127) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void draw_alpha_mask(fb_t *fb, int dst_x, int dst_y,
                     const uint8_t *mask, int w, int h, int mask_pitch,
                     uint32_t fg) {
    int sx0 = 0, sy0 = 0;
    if (dst_x < 0) { sx0 = -dst_x; dst_x = 0; }
    if (dst_y < 0) { sy0 = -dst_y; dst_y = 0; }
    int x1 = dst_x + (w - sx0);
    int y1 = dst_y + (h - sy0);
    if (x1 > fb->w) x1 = fb->w;
    if (y1 > fb->h) y1 = fb->h;

    for (int yy = dst_y, sy = sy0; yy < y1; yy++, sy++) {
        uint32_t *drow      = fb->px + (size_t)yy * fb->stride + dst_x;
        const uint8_t *srow = mask  + (size_t)sy * mask_pitch  + sx0;
        for (int xx = dst_x; xx < x1; xx++) {
            uint8_t a = *srow++;
            if (a) *drow = blend_over(*drow, fg, a);
            drow++;
        }
    }
}

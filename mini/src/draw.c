#include "draw.h"
#include <math.h>
#include <stdlib.h>
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

static inline void put_px(fb_t *fb, int x, int y, uint32_t color) {
    if ((unsigned)x < (unsigned)fb->w && (unsigned)y < (unsigned)fb->h) {
        fb->px[(size_t)y * fb->stride + x] = color;
    }
}

void draw_circle(fb_t *fb, int cx, int cy, int r, uint32_t color) {
    if (r <= 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        put_px(fb, cx + x, cy + y, color); put_px(fb, cx - x, cy + y, color);
        put_px(fb, cx + x, cy - y, color); put_px(fb, cx - x, cy - y, color);
        put_px(fb, cx + y, cy + x, color); put_px(fb, cx - y, cy + x, color);
        put_px(fb, cx + y, cy - x, color); put_px(fb, cx - y, cy - x, color);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x + 1); }
    }
}

void draw_ellipse(fb_t *fb, int cx, int cy, int a, int b, int clip_r,
                  uint32_t color) {
    if (a <= 0 || b <= 0) return;
    int cr2 = clip_r * clip_r;
    /* Parametric sweep — 1 sample per pixel of the longer axis is enough. */
    int steps = (a > b ? a : b) * 4;
    if (steps < 16) steps = 16;
    double last_px = 0, last_py = 0;
    int first = 1;
    for (int i = 0; i <= steps; i++) {
        double t  = (double)i / steps * 2.0 * M_PI;
        double dx = a * cos(t);
        double dy = b * sin(t);
        if (clip_r > 0 && dx * dx + dy * dy > cr2) {
            first = 1;
            continue;
        }
        int x = cx + (int)(dx + (dx >= 0 ? 0.5 : -0.5));
        int y = cy + (int)(dy + (dy >= 0 ? 0.5 : -0.5));
        put_px(fb, x, y, color);
        /* Fill any gap between consecutive samples with a short line so the
         * outline stays continuous on the wide axis. */
        if (!first) {
            int x0 = cx + (int)(last_px + (last_px >= 0 ? 0.5 : -0.5));
            int y0 = cy + (int)(last_py + (last_py >= 0 ? 0.5 : -0.5));
            if (abs(x - x0) > 1 || abs(y - y0) > 1)
                draw_line(fb, x0, y0, x, y, color);
        }
        last_px = dx; last_py = dy;
        first = 0;
    }
}

void draw_arc(fb_t *fb, int cx, int cy, int r,
              double a0, double a1, uint32_t color) {
    if (r <= 0) return;
    double span = fabs(a1 - a0);
    int    steps = (int)(r * span) + 8;
    int    last_x = 0, last_y = 0, first = 1;
    for (int i = 0; i <= steps; i++) {
        double t = a0 + (a1 - a0) * i / steps;
        int x = cx + (int)(r * cos(t) + 0.5);
        int y = cy + (int)(r * sin(t) + 0.5);
        put_px(fb, x, y, color);
        if (!first && (abs(x - last_x) > 1 || abs(y - last_y) > 1))
            draw_line(fb, last_x, last_y, x, y, color);
        last_x = x; last_y = y; first = 0;
    }
}

void draw_thick_line(fb_t *fb, int x0, int y0, int x1, int y1,
                     int thickness, uint32_t color) {
    /* For our 2-pixel needles a plain double-stroke offset by 1 pixel is
     * enough; we don't need full polygon thickening here. */
    draw_line(fb, x0, y0, x1, y1, color);
    if (thickness < 2) return;
    int dx = x1 - x0, dy = y1 - y0;
    if (abs(dx) >= abs(dy)) {
        draw_line(fb, x0, y0 + 1, x1, y1 + 1, color);
    } else {
        draw_line(fb, x0 + 1, y0, x1 + 1, y1, color);
    }
}

/* Departure HUD — minimal Wayland edition.
 *
 * Renders the same dashboard as the Qt build directly into a single shm
 * buffer: layer-shell fullscreen overlay, click-through, software-rasterised
 * text/lines/circles/arcs. No QML, no scene graph, no Mesa, no NVIDIA libs at
 * runtime unless the user has nvidia-ml installed (in which case we dlopen it
 * for GPU stats). */

#define _GNU_SOURCE

#include "wl.h"
#include "draw.h"
#include "font.h"
#include "sys.h"
#include "audio.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

/* The Qt HUD is designed against a 1180×600 surface; we keep the same so the
 * layout numbers are directly portable. */
#define HUD_W   1180
#define HUD_H   600

/* Background: fully transparent so the HUD floats over the desktop instead
 * of blacking out the whole screen. */
#define COL_BG      rgba(0x00, 0x00, 0x00, 0x00)
#define COL_BG_FILL rgba(0x0d, 0x0d, 0x0d, 0xE0)
#define COL_FG      rgba(0xf0, 0x8a, 0x28, 0xFF)
#define COL_FG_DIM  rgba(0x60, 0x37, 0x10, 0xFF)
#define COL_FG_SOFT rgba(0x28, 0x16, 0x07, 0xFF)
#define COL_HOT     rgba(0xff, 0x5a, 0x3c, 0xFF)
#define COL_PILL_FG rgba(0x0a, 0x0a, 0x0a, 0xFF)

/* Layout constants — mirror the QML scaler block. */
#define PAD_X   18
#define PAD_Y   10
#define GAP_C   14
#define GAP_R   8
#define LEFT_W  195
#define RIGHT_W 220
#define TOP_H   24
#define BOT_H   38

#define F_SMALL 9
#define F_BODY  11
#define F_LABEL 13

#define N_STARS 20

typedef struct {
    font_t *small;
    font_t *body;
    font_t *label;
} fonts_t;

typedef struct {
    double angle;     /* radians */
    double cos_a, sin_a;
    double radius;    /* in design units (max ~175) */
    double speed;     /* design units per second */
} star_t;

typedef struct {
    int mid_x, mid_w;
    int right_x;
    int mid_y, mid_row_h;
    int bot_y;
} layout_t;

static layout_t layout_compute(void) {
    layout_t L;
    L.mid_x     = PAD_X + LEFT_W + GAP_C;
    L.mid_w     = HUD_W - PAD_X * 2 - LEFT_W - RIGHT_W - GAP_C * 2;
    L.right_x   = L.mid_x + L.mid_w + GAP_C;
    L.mid_y     = PAD_Y + TOP_H + GAP_R;
    L.mid_row_h = HUD_H - PAD_Y * 2 - TOP_H - BOT_H - GAP_R * 2;
    L.bot_y     = L.mid_y + L.mid_row_h + GAP_R;
    return L;
}

/* ── Text helpers ───────────────────────────────────────────────────── */

static void txt(fb_t *fb, const font_t *f, int x, int y_top,
                const char *s, uint32_t color) {
    font_draw(fb, f, x, y_top + font_ascent(f), s, color);
}

static void txt_right(fb_t *fb, const font_t *f, int x_right, int y_top,
                      const char *s, uint32_t color) {
    int w = font_text_width(f, s);
    font_draw(fb, f, x_right - w, y_top + font_ascent(f), s, color);
}

static void txt_center(fb_t *fb, const font_t *f, int cx, int y_top,
                       const char *s, uint32_t color) {
    int w = font_text_width(f, s);
    font_draw(fb, f, cx - w / 2, y_top + font_ascent(f), s, color);
}

/* ── Section primitives ─────────────────────────────────────────────── */

static int section_header(fb_t *fb, fonts_t *F, int x, int y, int w,
                          const char *label) {
    int lw    = font_text_width(F->label, label);
    int pad   = font_cell_width(F->label);
    int gap_w = lw + pad * 2;
    int gap_x = x + (w - gap_w) / 2;
    draw_hline(fb, x,             y, gap_x - x,                 COL_FG);
    draw_hline(fb, gap_x + gap_w, y, x + w - (gap_x + gap_w),   COL_FG);
    /* Down-pointing ticks at both ends. */
    draw_vline(fb, x,         y, 7, COL_FG);
    draw_vline(fb, x + w - 1, y, 7, COL_FG);
    int box_h = font_line_height(F->label);
    font_draw(fb, F->label, gap_x + pad,
              y - box_h / 2 + font_ascent(F->label), label, COL_FG);
    return y + 10;
}

static int bar_row(fb_t *fb, fonts_t *F, int x, int y, int w,
                   double pct, const char *value, bool hot) {
    int val_w = font_text_width(F->body, value);
    int gap   = font_cell_width(F->body);
    int bar_w = w - val_w - gap;
    int bar_h = 8;
    int bar_y = y + (font_line_height(F->body) - bar_h) / 2;
    draw_rect(fb, x, bar_y, bar_w, bar_h, COL_FG_SOFT);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    int fill = (int)(bar_w * pct / 100.0 + 0.5);
    if (fill > 0) {
        draw_rect(fb, x, bar_y, fill, bar_h, hot ? COL_HOT : COL_FG);
        if (fill > 3) draw_vline(fb, x + fill - 3, bar_y, bar_h, COL_BG);
    }
    txt(fb, F->body, x + bar_w + gap, y, value, hot ? COL_HOT : COL_FG);
    return y + font_line_height(F->body) + 2;
}

static int kv_block(fb_t *fb, fonts_t *F, int x, int y, int w,
                    const char *k, const char *v, bool align_right) {
    if (align_right) {
        txt_right(fb, F->small, x + w, y, k, COL_FG);
        txt_right(fb, F->body,  x + w, y + font_line_height(F->small) - 2, v, COL_FG);
    } else {
        txt(fb, F->small, x, y, k, COL_FG);
        txt(fb, F->body,  x, y + font_line_height(F->small) - 2, v, COL_FG);
    }
    return y + font_line_height(F->small) + font_line_height(F->body) - 2;
}

static int kv_inline(fb_t *fb, fonts_t *F, int x, int y,
                     const char *k, const char *v) {
    txt(fb, F->body, x, y, k, COL_FG);
    int kw = font_text_width(F->body, k);
    txt(fb, F->body, x + kw + 4, y, v, COL_FG);
    int vw = font_text_width(F->body, v);
    return kw + 4 + vw;
}

/* ── Formatting ─────────────────────────────────────────────────────── */

static void fmt_rate(double bps, char *out, size_t cap) {
    if (bps < 1024)              snprintf(out, cap, "%.0f B", bps);
    else if (bps < 1024.0*1024)  snprintf(out, cap, "%.0f K", bps / 1024.0);
    else if (bps < 1024.0*1024*1024) {
        double m = bps / (1024.0 * 1024.0);
        snprintf(out, cap, m < 10 ? "%.1f M" : "%.0f M", m);
    } else {
        snprintf(out, cap, "%.2f G", bps / (1024.0 * 1024.0 * 1024.0));
    }
}

static double rate_bar_pct(double bps) {
    if (bps <= 0) return 0;
    double v = log10(1.0 + bps);
    return v / 8.0 * 100.0;
}

static void fmt_clock_wall(char *out, size_t cap) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(out, cap, "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void fmt_uptime(unsigned long s, char *out, size_t cap) {
    unsigned d = s / 86400;
    unsigned h = (s % 86400) / 3600;
    unsigned m = (s % 3600) / 60;
    unsigned r = s % 60;
    snprintf(out, cap, "%02uD %02u:%02u:%02u", d, h, m, r);
}

/* ── Volume knob (semicircle + needle + OVERLOAD) ──────────────────── */

static void volume_knob(fb_t *fb, fonts_t *F, int box_x, int box_y,
                        int volume_pct, bool muted) {
    int w = 110, h = 58;
    int cx = box_x + w / 2;
    int cy = box_y + h - 6;
    int r  = (h < w ? h : w) - 16;
    bool overload = volume_pct > 100;

    draw_arc(fb, cx, cy, r, M_PI, 2 * M_PI, COL_FG_DIM);

    int v = volume_pct;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    double ang = M_PI + (v / 100.0) * M_PI;
    int nx = cx + (int)(cos(ang) * (r - 2));
    int ny = cy + (int)(sin(ang) * (r - 2));
    uint32_t ncol = overload ? COL_HOT : (muted ? COL_FG_DIM : COL_FG);
    draw_thick_line(fb, cx, cy, nx, ny, 2, ncol);
    draw_rect(fb, cx - 2, cy - 2, 4, 4, ncol);

    char buf[16];
    snprintf(buf, sizeof(buf), muted ? "MUTE" : "%d", volume_pct);
    txt_center(fb, F->body, cx, box_y + h - font_line_height(F->body), buf, ncol);

    if (overload) {
        int oy = cy - r / 2 - font_line_height(F->body) / 2;
        txt_center(fb, F->body, cx, oy, "OVERLOAD", COL_HOT);
    }
}

/* ── AUDIO section ─────────────────────────────────────────────────── */

static int audio_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                         const audio_state_t *au) {
    y = section_header(fb, F, x, y, w, "AUDIO");
    txt_center(fb, F->small, x + w / 2, y, "MASTER VOL", COL_FG);
    y += font_line_height(F->small) + 4;

    int box_x = x + (w - 110) / 2;
    volume_knob(fb, F, box_x, y,
                au->has_audio ? au->volume_pct : 0,
                au->has_audio ? au->muted      : false);
    y += 58 + 4;

    /* Output buttons: SP HP BT HD; first one lit as Qt does. */
    int bw = 28, bh = 24, bg = 5;
    int total = 4 * bw + 3 * bg;
    int bx = x + (w - total) / 2;
    static const char *labels[] = { "SP", "HP", "BT", "HD" };
    for (int i = 0; i < 4; i++) {
        bool on = (i == 0);
        draw_rect(fb, bx, y, bw, bh, on ? COL_FG : COL_BG);
        draw_frame(fb, bx, y, bw, bh, COL_FG);
        txt_center(fb, F->body, bx + bw / 2,
                   y + (bh - font_line_height(F->body)) / 2,
                   labels[i], on ? COL_PILL_FG : COL_FG);
        bx += bw + bg;
    }
    y += bh + 2;
    txt_center(fb, F->small, x + w / 2, y, "OUTPUT", COL_FG);
    return y + font_line_height(F->small);
}

/* ── Data-driven sections (CPU / MEM / GPU / THERM / BATT / DISK / NET / DISPLAY) ── */

static int cpu_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                       const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "CPU");
    txt(fb, F->small, x, y, "CORE LOAD %", COL_FG);
    y += font_line_height(F->small);
    int rows = si->cpu_n;
    if (rows > 8) rows = 8;
    for (int i = 0; i < rows; i++) {
        char v[16];
        snprintf(v, sizeof(v), "%02d%%", (int)(si->cpu_per[i] + 0.5));
        y = bar_row(fb, F, x, y, w, si->cpu_per[i], v, si->cpu_per[i] > 88);
    }
    if (si->cpu_n > 0) {
        txt(fb, F->small, x, y + 4, "FREQ GHz", COL_FG);
        y += font_line_height(F->small) + 4;
        int show = si->cpu_n < 2 ? si->cpu_n : 2;
        double maxv = si->freq_max_ghz > 0 ? si->freq_max_ghz : 4.0;
        for (int i = 0; i < show; i++) {
            char v[16]; snprintf(v, sizeof(v), "%.2f", si->freq_ghz[i]);
            double pct = si->freq_ghz[i] / maxv * 100.0;
            y = bar_row(fb, F, x, y, w, pct, v, false);
        }
    }
    return y;
}

static int mem_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                       const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "MEM");
    txt(fb, F->small, x, y, "USED / CACHE / SWAP", COL_FG);
    y += font_line_height(F->small);
    char v[16];
    snprintf(v, sizeof(v), "%.1fG", si->mem_used_gb);
    y = bar_row(fb, F, x, y, w, si->mem_used_pct, v, si->mem_used_pct > 90);
    snprintf(v, sizeof(v), "%.1fG", si->mem_cache_gb);
    y = bar_row(fb, F, x, y, w, si->mem_cache_pct, v, false);
    snprintf(v, sizeof(v), "%.1fG", si->swap_used_gb);
    y = bar_row(fb, F, x, y, w, si->swap_used_pct, v, false);
    return y;
}

static int gpu_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                       const sysinfo_t *si) {
    if (!si->has_gpu) return y;
    y = section_header(fb, F, x, y, w, "GPU");
    txt(fb, F->small, x, y, "LOAD %", COL_FG);
    y += font_line_height(F->small);
    char v[16]; snprintf(v, sizeof(v), "%02d%%", (int)(si->gpu_load_pct + 0.5));
    y = bar_row(fb, F, x, y, w, si->gpu_load_pct, v, si->gpu_load_pct > 90);
    if (si->gpu_clock_mhz > 0) {
        txt(fb, F->small, x, y + 4, "CLOCK MHz", COL_FG);
        y += font_line_height(F->small) + 4;
        snprintf(v, sizeof(v), "%d", si->gpu_clock_mhz);
        double pct = si->gpu_clock_max_mhz > 0
            ? (double)si->gpu_clock_mhz / si->gpu_clock_max_mhz * 100.0 : 0;
        y = bar_row(fb, F, x, y, w, pct, v, false);
    }
    return y;
}

static int therm_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                         const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "THERM");
    struct { const char *k; bool has; double v; double max; } rows[] = {
        { "CPU",     si->has_cpu_temp,  si->cpu_temp_c,  90 },
        { "GPU",     si->has_gpu_temp,  si->gpu_temp_c,  85 },
        { "SSD",     si->has_ssd_temp,  si->ssd_temp_c,  65 },
        { "CHASSIS", si->has_case_temp, si->case_temp_c, 50 },
    };
    int col_v = x + 70, col_m = x + w - 28;
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        if (!rows[i].has) continue;
        txt(fb, F->body, x, y, rows[i].k, COL_FG);
        char v[16]; snprintf(v, sizeof(v), "%d C", (int)(rows[i].v + 0.5));
        bool hot = rows[i].v > rows[i].max * 0.95;
        txt(fb, F->body, col_v, y, v, hot ? COL_HOT : COL_FG);
        char m[8]; snprintf(m, sizeof(m), "%d", (int)rows[i].max);
        txt_right(fb, F->body, col_m + 28, y, m, COL_FG);
        y += font_line_height(F->body) + 2;
    }
    return y;
}

static int batt_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                        const sysinfo_t *si) {
    if (!si->has_battery) return y;
    y = section_header(fb, F, x, y, w, "BATT");
    txt(fb, F->small, x, y, "CHARGE %", COL_FG);
    y += font_line_height(F->small);
    char v[16]; snprintf(v, sizeof(v), "%03d", (int)(si->bat_pct + 0.5));
    y = bar_row(fb, F, x, y, w, si->bat_pct, v, si->bat_pct < 20);

    txt(fb, F->small, x, y, "DRAIN %/H", COL_FG);
    y += font_line_height(F->small);
    snprintf(v, sizeof(v), "%.1f/H", si->bat_drain_pct_h);
    double drain_pct = si->bat_drain_pct_h / 30.0 * 100.0;
    y = bar_row(fb, F, x, y, w, drain_pct, v, si->bat_drain_pct_h > 20);

    int half = (w - 10) / 2;
    int y2 = y + 4;
    kv_block(fb, F, x,          y2, half, "STATE", si->bat_state, false);
    char rate[32];
    snprintf(rate, sizeof(rate), "%s%.1fW",
             si->bat_rate_w >= 0 ? "+" : "-", fabs(si->bat_rate_w));
    kv_block(fb, F, x + half + 10, y2, half, "RATE", rate, true);

    int y3 = y2 + font_line_height(F->small) + font_line_height(F->body) + 4;
    char tl[16];
    snprintf(tl, sizeof(tl), "%02d:%02d",
             si->bat_minutes_left / 60, si->bat_minutes_left % 60);
    kv_block(fb, F, x,             y3, half, "TIME LEFT", tl, false);
    char cyc[16]; snprintf(cyc, sizeof(cyc), "%d", si->bat_cycles);
    kv_block(fb, F, x + half + 10, y3, half, "CYCLES", cyc, true);
    return y3 + font_line_height(F->small) + font_line_height(F->body);
}

static int disk_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                        const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "DISK");
    char mounts[160] = "";
    for (int i = 0; i < si->disks_n; i++) {
        strncat(mounts, si->disks[i].mount,
                sizeof(mounts) - strlen(mounts) - 4);
        strncat(mounts, "  ", sizeof(mounts) - strlen(mounts) - 1);
    }
    txt(fb, F->small, x, y, mounts, COL_FG);
    y += font_line_height(F->small);
    for (int i = 0; i < si->disks_n; i++) {
        char v[16]; snprintf(v, sizeof(v), "%d%%",
                             (int)(si->disks[i].used_pct + 0.5));
        y = bar_row(fb, F, x, y, w, si->disks[i].used_pct, v,
                    si->disks[i].used_pct > 90);
    }
    return y;
}

static int net_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                       const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "NET");
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "v DOWN / ^ UP  [%s]",
             si->net_iface[0] ? si->net_iface : "-");
    txt(fb, F->small, x, y, hdr, COL_FG);
    y += font_line_height(F->small);
    char v[24];
    fmt_rate(si->net_down_bps, v, sizeof(v));
    y = bar_row(fb, F, x, y, w, rate_bar_pct(si->net_down_bps), v, false);
    fmt_rate(si->net_up_bps, v, sizeof(v));
    y = bar_row(fb, F, x, y, w, rate_bar_pct(si->net_up_bps), v, false);
    return y;
}

static int display_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                           const sysinfo_t *si) {
    if (!si->has_brightness) return y;
    y = section_header(fb, F, x, y, w, "DISPLAY");
    txt(fb, F->small, x, y, "BRIGHTNESS", COL_FG);
    y += font_line_height(F->small);
    char v[16]; snprintf(v, sizeof(v), "%d%%", si->brightness_pct);
    y = bar_row(fb, F, x, y, w, si->brightness_pct, v, false);
    return y;
}

/* ── Year stack (left of scope) ─────────────────────────────────────── */

static void year_stack(fb_t *fb, fonts_t *F, int x, int y, int w, int h) {
    (void)h;
    txt_right(fb, F->small, x + w - 10, y, "YEAR", COL_FG);
    int start_y = y + font_line_height(F->small) + 6;
    int row_h   = 18;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int cur = tm->tm_year + 1900;
    for (int i = 0; i < 11; i++) {
        int year = 2020 + i;
        char buf[8]; snprintf(buf, sizeof(buf), "%d", year);
        int row_y = start_y + i * row_h;
        bool sel = (year == cur);
        uint32_t col = sel ? COL_FG : COL_FG_DIM;
        int yw = font_text_width(F->body, buf);
        int yx = x + w - 10 - yw;
        txt(fb, F->body, yx, row_y, buf, col);
        if (sel) {
            txt(fb, F->body, yx - font_cell_width(F->body), row_y, "[", COL_FG);
            txt(fb, F->body, yx + yw,                       row_y, "]", COL_FG);
        }
    }
}

/* ── Pitch stack (right of scope) ──────────────────────────────────── */

static void pitch_stack(fb_t *fb, fonts_t *F, int x, int y, int w, int h,
                        double t) {
    (void)h;
    static const int scale[] = { 30, 25, 20, 15, 10, 5, 0,
                                -5, -10, -15, -20, -25, -30 };
    int n = sizeof(scale) / sizeof(scale[0]);
    int row_h = 18;

    txt(fb, F->small, x + 10, y, "ACCEL", COL_FG);
    txt(fb, F->small, x + 10, y + font_line_height(F->small), "FPS2", COL_FG);
    int start_y = y + 2 * font_line_height(F->small) + 6;

    double v =
        sin(t * 0.16)         * 18.0 +
        sin(t * 0.34 + 1.5)   *  9.0 +
        sin(t * 0.56 + 0.7)   *  4.0;
    double f = (30.0 - v) / 60.0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    int near_idx = (int)(f * (n - 1) + 0.5);

    for (int i = 0; i < n; i++) {
        char buf[8];
        char sign = scale[i] > 0 ? '+' : (scale[i] < 0 ? '-' : ' ');
        snprintf(buf, sizeof(buf), "%c%02d", sign, abs(scale[i]));
        uint32_t col = (i == near_idx) ? COL_FG : COL_FG_DIM;
        txt(fb, F->body, x + 10, start_y + i * row_h, buf, col);
    }

    /* Animated marker — uses '<' since the font may not have ◂. */
    int marker_y = start_y + (int)(f * (n - 1) * row_h)
                          - font_line_height(F->label) / 2 + row_h / 2;
    txt(fb, F->label, x + 38, marker_y, "<", COL_FG);
}

/* ── Stars ─────────────────────────────────────────────────────────── */

static double frand(void) { return (double)rand() / (double)RAND_MAX; }

static void star_spawn(star_t *s) {
    s->angle  = frand() * 2.0 * M_PI;
    s->cos_a  = cos(s->angle);
    s->sin_a  = sin(s->angle);
    s->speed  = 35.0 + frand() * 60.0;
}

static void stars_init(star_t *arr, int n) {
    srand((unsigned)time(NULL));
    for (int i = 0; i < n; i++) {
        star_spawn(&arr[i]);
        arr[i].radius = frand() * 175.0;
    }
}

static void stars_step(star_t *arr, int n, double dt) {
    for (int i = 0; i < n; i++) {
        arr[i].radius += arr[i].speed * dt;
        if (arr[i].radius > 175.0) {
            star_spawn(&arr[i]);
            arr[i].radius = 1.0;
        }
    }
}

/* ── Scope (sphere wireframe + stars + readout + AUTO label) ───────── */

/* Scope is laid out against a 480x460 design canvas; we scale it to whatever
 * space we get between the year and pitch stacks. */
#define VB_W 480
#define VB_H 460

static void scope_render(fb_t *fb, fonts_t *F, int x, int y, int w, int h,
                         const star_t *stars, int n_stars,
                         const sysinfo_t *si) {
    double sx = (double)w / VB_W;
    double sy = (double)h / VB_H;
    double unit = sx < sy ? sx : sy;
    int cx = x + (int)(240 * sx);
    int cy = y + (int)(230 * sy);
    int R  = (int)(180 * unit);

    /* Corner brackets. */
    #define PT(px, py) x + (int)((px) * sx), y + (int)((py) * sy)
    draw_line(fb, PT(45,130), PT(30,145), COL_FG);
    draw_line(fb, PT(30,145), PT(30,315), COL_FG);
    draw_line(fb, PT(30,315), PT(45,330), COL_FG);
    draw_line(fb, PT(435,130), PT(450,145), COL_FG);
    draw_line(fb, PT(450,145), PT(450,315), COL_FG);
    draw_line(fb, PT(450,315), PT(435,330), COL_FG);
    /* Side ticks. */
    draw_line(fb, PT(30,230), PT(37,230), COL_FG);
    draw_line(fb, PT(450,230), PT(443,230), COL_FG);

    /* Outer circle and the two clipped meridians. */
    draw_circle(fb, cx, cy, R, COL_FG);
    draw_ellipse(fb, cx, cy, (int)(280 * sx), (int)(130 * sy), R, COL_FG);
    draw_ellipse(fb, cx, cy, (int)(130 * sx), (int)(280 * sy), R, COL_FG);

    /* Cross axes through center. */
    draw_line(fb, PT(48,230), PT(432,230), COL_FG);
    draw_line(fb, PT(240,42), PT(240,418), COL_FG);

    /* Center crosshair (4 short marks). */
    draw_line(fb, cx-12, cy, cx-4,  cy, COL_FG);
    draw_line(fb, cx+4,  cy, cx+12, cy, COL_FG);
    draw_line(fb, cx, cy-12, cx, cy-4,  COL_FG);
    draw_line(fb, cx, cy+4,  cx, cy+12, COL_FG);
    #undef PT

    /* Stars — simple rectangles, alpha approximated by size only. */
    double max_r_px = 175.0 * unit;
    for (int i = 0; i < n_stars; i++) {
        double r_px = stars[i].radius * unit;
        if (r_px <= 0 || r_px > max_r_px) continue;
        double frac = stars[i].radius / 175.0;
        int sz = 1 + (int)(frac * 3.5);
        if (sz < 1) sz = 1;
        if (sz > 4) sz = 4;
        int px = cx + (int)(stars[i].cos_a * r_px);
        int py = cy + (int)(stars[i].sin_a * r_px);
        draw_rect(fb, px - sz / 2, py - sz / 2, sz, sz, COL_FG);
    }

    /* NET/CPU/GPU readout in the upper-right of the scope. */
    char l1[24], l2[24], l3[24];
    int net_active = 0;
    {
        double sum = si->net_down_bps + si->net_up_bps;
        net_active = sum > 0 ? (int)(log10(1.0 + sum) * 12.0 + 0.5) : 0;
        if (net_active > 99) net_active = 99;
    }
    snprintf(l1, sizeof(l1), "NET %02d",  net_active);
    snprintf(l2, sizeof(l2), "CPU %03d",  (int)(si->cpu_avg + 0.5));
    snprintf(l3, sizeof(l3), "GPU %03d",  (int)(si->gpu_load_pct + 0.5));

    int rh   = font_line_height(F->body);
    int rw   = font_text_width(F->body, "GPU 999") + 14;
    int rx   = x + (int)(w * 0.62);
    int ry   = y + (int)(h * 0.20);
    int boxh = 3 * rh + 6;
    draw_rect(fb, rx, ry, rw, boxh, COL_BG_FILL);
    draw_frame(fb, rx, ry, rw, boxh, COL_FG);
    txt(fb, F->body, rx + 7, ry + 3,            l1, COL_FG);
    txt(fb, F->body, rx + 7, ry + 3 + rh,       l2, COL_FG);
    txt(fb, F->body, rx + 7, ry + 3 + 2 * rh,   l3, COL_FG);

    /* AUTO pill at the bottom. */
    {
        const char *p = "A U T O";
        int pad = 9;
        int pw = font_text_width(F->body, p) + pad * 2;
        int ph = rh + 4;
        int px = cx - pw / 2;
        int py = y + h - (int)(h * 0.09) - ph / 2;
        draw_rect(fb, px, py, pw, ph, COL_FG);
        txt_center(fb, F->body, cx, py + 2, p, COL_PILL_FG);
    }
}

/* ── Composite frame ────────────────────────────────────────────────── */

static void render(fb_t *fb, fonts_t *F,
                   const sysinfo_t *si, const audio_state_t *au,
                   const star_t *stars, int n_stars, double t_sec) {
    /* In the layer-shell HUD-sized window case ox/oy == 0; we still compute
     * them generically so the renderer works when called with a larger
     * surface (e.g. fullscreen test mode). */
    int ox = (fb->w - HUD_W) / 2; if (ox < 0) ox = 0;
    int oy = (fb->h - HUD_H) / 2; if (oy < 0) oy = 0;

    /* Slightly opaque backdrop behind the HUD so the orange text stays
     * legible over bright wallpapers. Outside the HUD bounds the surface
     * stays at its mmap-initialized 0x00000000 (fully transparent). */
    draw_rect(fb, ox, oy, HUD_W, HUD_H, COL_BG_FILL);

    layout_t L = layout_compute();

    /* ── Top row ────────────────────────────────────────────────── */
    char buf[64];
    fmt_clock_wall(buf, sizeof(buf));
    txt(fb, F->small, ox + PAD_X, oy + PAD_Y,                       "SYS TIME:", COL_FG);
    txt(fb, F->label, ox + PAD_X, oy + PAD_Y + font_line_height(F->small), buf, COL_FG);

    int title_l = ox + L.mid_x;
    int title_r = ox + L.mid_x + L.mid_w;
    int title_w = font_text_width(F->label, "S Y S M O N");
    int title_cx = title_l + L.mid_w / 2;
    txt_center(fb, F->label, title_cx, oy + PAD_Y + 4, "S Y S M O N", COL_FG);
    /* Side ticks. */
    int tick_y = oy + PAD_Y + 6;
    int n_ticks = 14;
    int gap_l = (title_cx - title_w / 2 - 8) - title_l;
    int gap_r = title_r - (title_cx + title_w / 2 + 8);
    int step_l = gap_l / n_ticks; if (step_l < 6) step_l = 6;
    int step_r = gap_r / n_ticks; if (step_r < 6) step_r = 6;
    for (int i = 0; i < n_ticks; i++) {
        uint32_t c = (i % 3 == 1) ? COL_FG_DIM : COL_FG;
        draw_vline(fb, title_l + i * step_l + 4,                       tick_y, 9, c);
        draw_vline(fb, title_cx + title_w / 2 + 8 + i * step_r,        tick_y, 9, c);
    }

    fmt_uptime(si->uptime_sec, buf, sizeof(buf));
    txt_right(fb, F->small, ox + L.right_x + RIGHT_W, oy + PAD_Y,
              "UPTIME:", COL_FG);
    txt_right(fb, F->label, ox + L.right_x + RIGHT_W,
              oy + PAD_Y + font_line_height(F->small), buf, COL_FG);

    /* ── Left column: CPU / MEM / AUDIO ─────────────────────────── */
    int y = oy + L.mid_y;
    y = cpu_section  (fb, F, ox + PAD_X, y,      LEFT_W, si);
    y = mem_section  (fb, F, ox + PAD_X, y + 12, LEFT_W, si);
    y = audio_section(fb, F, ox + PAD_X, y + 12, LEFT_W, au);

    /* ── Right column: GPU / THERM / BATT / DISK / NET / DISPLAY ─ */
    int yr = oy + L.mid_y;
    if (si->has_gpu) yr = gpu_section(fb, F, ox + L.right_x, yr, RIGHT_W, si);
    yr = therm_section(fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    if (si->has_battery)
        yr = batt_section(fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    yr = disk_section (fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    yr = net_section  (fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    if (si->has_brightness)
        yr = display_section(fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);

    /* ── Middle: YearStack | Scope | PitchStack ─────────────────── */
    {
        int my = oy + L.mid_y;
        int mh = L.mid_row_h;
        int stack_w = 70;

        year_stack (fb, F, ox + L.mid_x,                       my, stack_w, mh);
        pitch_stack(fb, F, ox + L.mid_x + L.mid_w - stack_w,   my, stack_w, mh,
                    t_sec);

        int sx = ox + L.mid_x + stack_w;
        int sy = my;
        int sw = L.mid_w - stack_w * 2;
        int sh = mh;
        scope_render(fb, F, sx, sy, sw, sh, stars, n_stars, si);
    }

    /* ── Bottom row ─────────────────────────────────────────────── */
    int by = oy + L.bot_y + (BOT_H - font_line_height(F->body)) / 2;
    {
        int pad = 8;
        const char *p = "SYSMON";
        int pw = font_text_width(F->body, p) + pad * 2;
        int ph = font_line_height(F->body) + 4;
        int px = ox + PAD_X;
        draw_rect(fb, px, by - 2, pw, ph, COL_FG);
        txt(fb, F->body, px + pad, by - 2, p, COL_PILL_FG);
    }
    {
        const char *k[4] = { "HOST:", "KERNEL:", "SHELL:", "USER:" };
        const char *v[4] = { si->host, si->kernel, si->shell, si->user };
        int gap = 26;
        int kw[4], total = 0;
        for (int i = 0; i < 4; i++) {
            kw[i] = font_text_width(F->body, k[i]) + 4 +
                    font_text_width(F->body, v[i]);
            total += kw[i];
        }
        total += gap * 3;
        int cx = ox + L.mid_x + L.mid_w / 2 - total / 2;
        for (int i = 0; i < 4; i++)
            cx += kv_inline(fb, F, cx, by, k[i], v[i]) + gap;
    }
    {
        int rx = ox + L.right_x + RIGHT_W;
        txt_right(fb, F->small, rx, by - font_line_height(F->small) + 2,
                  "LOAD AVG:", COL_FG);
        char la[64];
        snprintf(la, sizeof(la), "%.2f %.2f %.2f",
                 si->load_avg[0], si->load_avg[1], si->load_avg[2]);
        txt_right(fb, F->body, rx, by, la, COL_FG);
    }
}

/* ── Font discovery ─────────────────────────────────────────────────── */

static const char *find_font(void) {
    static const char *candidates[] = {
        "fonts/DepartureMono-Regular.otf",
        "../fonts/DepartureMono-Regular.otf",
        "/usr/share/departure-hud/fonts/DepartureMono-Regular.otf",
        NULL,
    };
    for (int i = 0; candidates[i]; i++)
        if (access(candidates[i], R_OK) == 0) return candidates[i];
    return NULL;
}

/* ── Main ───────────────────────────────────────────────────────────── */

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    const char *fp = find_font();
    if (!fp) {
        fprintf(stderr,
            "departure-hud-mini: cannot find DepartureMono-Regular.otf.\n"
            "Run from the project root or symlink the font into ./fonts/.\n");
        return 1;
    }
    fonts_t F = {
        .small = font_open(fp, F_SMALL),
        .body  = font_open(fp, F_BODY),
        .label = font_open(fp, F_LABEL),
    };
    if (!F.small || !F.body || !F.label) {
        fprintf(stderr, "departure-hud-mini: cannot load font at %s\n", fp);
        return 1;
    }

    /* HUD-sized window centered by the compositor. Saves ~5 MB of shm
     * compared to a fullscreen overlay because the buffer is exactly the
     * HUD bounds, and we don't pay to clear pixels we won't touch. */
    wl_window_opts_t opts = {
        .width = HUD_W, .height = HUD_H,
        .layer = WL_LAYER_OVERLAY,
        .anchors = 0,
        .click_through = true,
        .namespace_ = "departure-hud",
    };
    wl_ctx_t *wl = wl_open(&opts);
    if (!wl) return 1;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (tfd < 0) { perror("timerfd"); return 1; }
    struct itimerspec it = {
        .it_value    = { 0, 100 * 1000 * 1000 },   /* 100 ms */
        .it_interval = { 0, 100 * 1000 * 1000 },
    };
    timerfd_settime(tfd, 0, &it, NULL);

    sys_init(NULL);
    audio_init();

    sysinfo_t     si = {0};
    audio_state_t au = {0};
    star_t        stars[N_STARS];
    stars_init(stars, N_STARS);

    sys_poll_fast(&si);
    sys_poll_slow(&si);
    sys_poll_gpu (&si);
    audio_poll   (&au);

    double t_start = monotonic_seconds();
    double t_prev  = t_start;

    {
        fb_t fb = wl_framebuffer(wl);
        render(&fb, &F, &si, &au, stars, N_STARS, 0.0);
        wl_commit(wl);
    }

    struct pollfd fds[2] = {
        { .fd = wl_fd(wl), .events = POLLIN },
        { .fd = tfd,       .events = POLLIN },
    };

    int  slow_counter = 0;
    bool need_redraw  = false;

    while (!wl_should_close(wl)) {
        int r = poll(fds, 2, -1);
        if (r < 0) { if (errno == EINTR) continue; perror("poll"); break; }

        if (fds[0].revents & POLLIN) wl_pump(wl);
        if (fds[1].revents & POLLIN) {
            uint64_t ticks; (void)!read(tfd, &ticks, sizeof(ticks));
            double now = monotonic_seconds();
            double dt = now - t_prev;
            if (dt < 0)   dt = 0;
            if (dt > 0.5) dt = 0.5;
            t_prev = now;

            stars_step(stars, N_STARS, dt);
            sys_poll_fast(&si);
            if (++slow_counter >= 10) {
                slow_counter = 0;
                sys_poll_slow(&si);
                sys_poll_gpu (&si);
                audio_poll   (&au);
            }
            need_redraw = true;
        }
        if (need_redraw && wl_frame_ready(wl)) {
            fb_t fb = wl_framebuffer(wl);
            render(&fb, &F, &si, &au, stars, N_STARS,
                   monotonic_seconds() - t_start);
            wl_commit(wl);
            need_redraw = false;
        }
    }

    close(tfd);
    audio_close();
    wl_close(wl);
    font_close(F.label);
    font_close(F.body);
    font_close(F.small);
    return 0;
}

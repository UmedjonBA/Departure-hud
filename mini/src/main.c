/* Departure HUD — minimal Wayland edition.
 *
 * Renders the same dashboard as the Qt build directly into a single shm
 * buffer: layer-shell surface, click-through, software-rasterised
 * text/lines/circles/arcs. No QML, no scene graph, no Mesa, no NVIDIA libs at
 * runtime unless the user opts into nvidia-ml for GPU stats.
 *
 * Like the Qt build, the 1180×600 design surface is scaled up to fill the
 * output (fonts are re-rendered at the scaled pixel size so text stays crisp;
 * we do NOT bitmap-upscale). */

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

/* Design surface — every layout number below is in these units and gets
 * multiplied by g_scale at draw time. */
#define HUD_W   1180
#define HUD_H   600

/* Theme (ARGB). Overridable via env before the first frame. */
static uint32_t COL_BG_FILL = 0xFF0d0d0du;   /* opaque dark panel (Qt default) */
static uint32_t COL_FG      = 0xFFf08a28u;
static uint32_t COL_FG_DIM  = 0xFF603710u;
static uint32_t COL_FG_SOFT = 0xFF281607u;
static uint32_t COL_HOT     = 0xFFff5a3cu;
static uint32_t COL_PILL_FG = 0xFF0a0a0au;

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

/* Global scale: 1.0 in native (corner) mode, min(W/1180,H/600) fullscreen. */
static double g_scale = 1.0;
static inline int SC(double v) { return (int)(v * g_scale + 0.5); }

typedef struct {
    font_t *small;
    font_t *body;
    font_t *label;
} fonts_t;

typedef struct {
    double angle;
    double cos_a, sin_a;
    double radius;    /* design units (max ~175) */
    double speed;     /* design units per second */
} star_t;

/* Layout geometry in *design* units (scale applied per-draw). */
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

/* ── Text helpers (x/y already in screen pixels) ────────────────────── */

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

/* ── Section primitives. (x,y,w) are *screen* pixels; internal offsets
 *    scaled via SC(); fonts already loaded at scaled size. ───────────── */

static int section_header(fb_t *fb, fonts_t *F, int x, int y, int w,
                          const char *label) {
    int lw    = font_text_width(F->label, label);
    int pad   = font_cell_width(F->label);
    int gap_w = lw + pad * 2;
    int gap_x = x + (w - gap_w) / 2;
    draw_hline(fb, x,             y, gap_x - x,                 COL_FG);
    draw_hline(fb, gap_x + gap_w, y, x + w - (gap_x + gap_w),   COL_FG);
    draw_vline(fb, x,         y, SC(7), COL_FG);
    draw_vline(fb, x + w - 1, y, SC(7), COL_FG);
    int box_h = font_line_height(F->label);
    font_draw(fb, F->label, gap_x + pad,
              y - box_h / 2 + font_ascent(F->label), label, COL_FG);
    return y + SC(10);
}

static int bar_row(fb_t *fb, fonts_t *F, int x, int y, int w,
                   double pct, const char *value, bool hot) {
    int val_w = font_text_width(F->body, value);
    int gap   = font_cell_width(F->body);
    int bar_w = w - val_w - gap;
    int bar_h = SC(8);
    int bar_y = y + (font_line_height(F->body) - bar_h) / 2;
    draw_rect(fb, x, bar_y, bar_w, bar_h, COL_FG_SOFT);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    int fill = (int)(bar_w * pct / 100.0 + 0.5);
    if (fill > 0) {
        draw_rect(fb, x, bar_y, fill, bar_h, hot ? COL_HOT : COL_FG);
        int notch = SC(3);
        if (fill > notch) draw_vline(fb, x + fill - notch, bar_y, bar_h, COL_BG_FILL);
    }
    txt(fb, F->body, x + bar_w + gap, y, value, hot ? COL_HOT : COL_FG);
    return y + font_line_height(F->body) + SC(2);
}

static int kv_block(fb_t *fb, fonts_t *F, int x, int y, int w,
                    const char *k, const char *v, bool align_right) {
    if (align_right) {
        txt_right(fb, F->small, x + w, y, k, COL_FG);
        txt_right(fb, F->body,  x + w, y + font_line_height(F->small) - SC(2), v, COL_FG);
    } else {
        txt(fb, F->small, x, y, k, COL_FG);
        txt(fb, F->body,  x, y + font_line_height(F->small) - SC(2), v, COL_FG);
    }
    return y + font_line_height(F->small) + font_line_height(F->body) - SC(2);
}

static int kv_inline(fb_t *fb, fonts_t *F, int x, int y,
                     const char *k, const char *v) {
    txt(fb, F->body, x, y, k, COL_FG);
    int kw = font_text_width(F->body, k);
    txt(fb, F->body, x + kw + SC(4), y, v, COL_FG);
    int vw = font_text_width(F->body, v);
    return kw + SC(4) + vw;
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
    return log10(1.0 + bps) / 8.0 * 100.0;
}
static void fmt_clock_wall(char *out, size_t cap) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(out, cap, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}
static void fmt_uptime(unsigned long s, char *out, size_t cap) {
    snprintf(out, cap, "%02luD %02lu:%02lu:%02lu",
             s / 86400, (s % 86400) / 3600, (s % 3600) / 60, s % 60);
}

/* ── Volume knob ────────────────────────────────────────────────────── */

static void volume_knob(fb_t *fb, fonts_t *F, int box_x, int box_y,
                        int volume_pct, bool muted) {
    int w = SC(110), h = SC(58);
    int cx = box_x + w / 2;
    int cy = box_y + h - SC(6);
    int r  = (h < w ? h : w) - SC(16);
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

static int audio_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                         const audio_state_t *au) {
    y = section_header(fb, F, x, y, w, "AUDIO");
    txt_center(fb, F->small, x + w / 2, y, "MASTER VOL", COL_FG);
    y += font_line_height(F->small) + SC(4);

    int box_x = x + (w - SC(110)) / 2;
    volume_knob(fb, F, box_x, y,
                au->has_audio ? au->volume_pct : 0,
                au->has_audio ? au->muted      : false);
    y += SC(58) + SC(4);

    int bw = SC(28), bh = SC(24), bg = SC(5);
    int total = 4 * bw + 3 * bg;
    int bx = x + (w - total) / 2;
    static const char *labels[] = { "SP", "HP", "BT", "HD" };
    for (int i = 0; i < 4; i++) {
        bool on = (i == 0);
        draw_rect(fb, bx, y, bw, bh, on ? COL_FG : COL_BG_FILL);
        draw_frame(fb, bx, y, bw, bh, COL_FG);
        txt_center(fb, F->body, bx + bw / 2,
                   y + (bh - font_line_height(F->body)) / 2,
                   labels[i], on ? COL_PILL_FG : COL_FG);
        bx += bw + bg;
    }
    y += bh + SC(2);
    txt_center(fb, F->small, x + w / 2, y, "OUTPUT", COL_FG);
    return y + font_line_height(F->small);
}

/* ── Data sections ──────────────────────────────────────────────────── */

static int cpu_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                       const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "CPU");
    txt(fb, F->small, x, y, "CORE LOAD %", COL_FG);
    y += font_line_height(F->small);
    /* Show every core, like the Qt build (it iterates all cpuPercents). */
    for (int i = 0; i < si->cpu_n; i++) {
        char v[16];
        snprintf(v, sizeof(v), "%02d%%", (int)(si->cpu_per[i] + 0.5));
        y = bar_row(fb, F, x, y, w, si->cpu_per[i], v, si->cpu_per[i] > 88);
    }
    if (si->cpu_n > 0) {
        txt(fb, F->small, x, y + SC(4), "FREQ GHz", COL_FG);
        y += font_line_height(F->small) + SC(4);
        int show = si->cpu_n < 2 ? si->cpu_n : 2;
        double maxv = si->freq_max_ghz > 0 ? si->freq_max_ghz : 4.0;
        for (int i = 0; i < show; i++) {
            char v[16]; snprintf(v, sizeof(v), "%.2f", si->freq_ghz[i]);
            y = bar_row(fb, F, x, y, w, si->freq_ghz[i] / maxv * 100.0, v, false);
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
        txt(fb, F->small, x, y + SC(4), "CLOCK MHz", COL_FG);
        y += font_line_height(F->small) + SC(4);
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
    int col_v = x + SC(70), col_m = x + w - SC(28);
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        if (!rows[i].has) continue;
        txt(fb, F->body, x, y, rows[i].k, COL_FG);
        char v[16]; snprintf(v, sizeof(v), "%d C", (int)(rows[i].v + 0.5));
        bool hot = rows[i].v > rows[i].max * 0.95;
        txt(fb, F->body, col_v, y, v, hot ? COL_HOT : COL_FG);
        char m[8]; snprintf(m, sizeof(m), "%d", (int)rows[i].max);
        txt_right(fb, F->body, col_m + SC(28), y, m, COL_FG);
        y += font_line_height(F->body) + SC(2);
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
    y = bar_row(fb, F, x, y, w, si->bat_drain_pct_h / 30.0 * 100.0, v,
                si->bat_drain_pct_h > 20);

    int half = (w - SC(10)) / 2;
    int y2 = y + SC(4);
    kv_block(fb, F, x,            y2, half, "STATE", si->bat_state, false);
    char rate[32];
    snprintf(rate, sizeof(rate), "%s%.1fW",
             si->bat_rate_w >= 0 ? "+" : "\xe2\x88\x92", fabs(si->bat_rate_w));
    kv_block(fb, F, x + half + SC(10), y2, half, "RATE", rate, true);

    int y3 = y2 + font_line_height(F->small) + font_line_height(F->body) + SC(4);
    char tl[16];
    snprintf(tl, sizeof(tl), "%02d:%02d",
             si->bat_minutes_left / 60, si->bat_minutes_left % 60);
    kv_block(fb, F, x,                y3, half, "TIME LEFT", tl, false);
    char cyc[16]; snprintf(cyc, sizeof(cyc), "%d", si->bat_cycles);
    kv_block(fb, F, x + half + SC(10), y3, half, "CYCLES", cyc, true);
    return y3 + font_line_height(F->small) + font_line_height(F->body);
}

static int disk_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                        const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "DISK");
    char mounts[160] = "";
    for (int i = 0; i < si->disks_n; i++) {
        strncat(mounts, si->disks[i].mount, sizeof(mounts) - strlen(mounts) - 4);
        strncat(mounts, "  ", sizeof(mounts) - strlen(mounts) - 1);
    }
    txt(fb, F->small, x, y, mounts, COL_FG);
    y += font_line_height(F->small);
    for (int i = 0; i < si->disks_n; i++) {
        char v[16]; snprintf(v, sizeof(v), "%d%%", (int)(si->disks[i].used_pct + 0.5));
        y = bar_row(fb, F, x, y, w, si->disks[i].used_pct, v,
                    si->disks[i].used_pct > 90);
    }
    return y;
}

static int net_section(fb_t *fb, fonts_t *F, int x, int y, int w,
                       const sysinfo_t *si) {
    y = section_header(fb, F, x, y, w, "NET");
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "\xe2\x86\x93 DOWN / \xe2\x86\x91 UP  [%s]",
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

/* ── Year / pitch stacks ────────────────────────────────────────────── */

static void year_stack(fb_t *fb, fonts_t *F, int x, int y, int w, int h) {
    (void)h;
    txt_right(fb, F->small, x + w - SC(10), y, "YEAR", COL_FG);
    int start_y = y + font_line_height(F->small) + SC(6);
    int row_h   = SC(18);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int cur = tm->tm_year + 1900;
    for (int i = 0; i < 11; i++) {
        int year = 2020 + i;
        char buf[8]; snprintf(buf, sizeof(buf), "%d", year);
        int row_y = start_y + i * row_h;
        bool sel = (year == cur);
        int yw = font_text_width(F->body, buf);
        int yx = x + w - SC(10) - yw;
        txt(fb, F->body, yx, row_y, buf, sel ? COL_FG : COL_FG_DIM);
        if (sel) {
            txt(fb, F->body, yx - font_cell_width(F->body), row_y, "[", COL_FG);
            txt(fb, F->body, yx + yw,                       row_y, "]", COL_FG);
        }
    }
}

static void pitch_stack(fb_t *fb, fonts_t *F, int x, int y, int w, int h,
                        double t) {
    (void)w; (void)h;
    static const int scale[] = { 30, 25, 20, 15, 10, 5, 0,
                                -5, -10, -15, -20, -25, -30 };
    int n = sizeof(scale) / sizeof(scale[0]);
    int row_h = SC(18);

    txt(fb, F->small, x + SC(10), y, "ACCEL", COL_FG);
    txt(fb, F->small, x + SC(10), y + font_line_height(F->small), "FPS2", COL_FG);
    int start_y = y + 2 * font_line_height(F->small) + SC(6);

    double v = sin(t * 0.16) * 18.0 + sin(t * 0.34 + 1.5) * 9.0 +
               sin(t * 0.56 + 0.7) * 4.0;
    double f = (30.0 - v) / 60.0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    int near_idx = (int)(f * (n - 1) + 0.5);

    for (int i = 0; i < n; i++) {
        char buf[8];
        char sign = scale[i] > 0 ? '+' : (scale[i] < 0 ? '-' : ' ');
        snprintf(buf, sizeof(buf), "%c%02d", sign, abs(scale[i]));
        txt(fb, F->body, x + SC(10), start_y + i * row_h, buf,
            (i == near_idx) ? COL_FG : COL_FG_DIM);
    }
    int marker_y = start_y + (int)(f * (n - 1) * row_h)
                          - font_line_height(F->label) / 2 + row_h / 2;
    txt(fb, F->label, x + SC(38), marker_y, "\xe2\x97\x82", COL_FG);
}

/* ── Stars ─────────────────────────────────────────────────────────── */

static double frand(void) { return (double)rand() / (double)RAND_MAX; }

static void star_spawn(star_t *s) {
    s->angle = frand() * 2.0 * M_PI;
    s->cos_a = cos(s->angle);
    s->sin_a = sin(s->angle);
    s->speed = 35.0 + frand() * 60.0;
}
static void stars_init(star_t *arr, int n) {
    srand((unsigned)time(NULL));
    for (int i = 0; i < n; i++) { star_spawn(&arr[i]); arr[i].radius = frand() * 175.0; }
}
static void stars_step(star_t *arr, int n, double dt) {
    for (int i = 0; i < n; i++) {
        arr[i].radius += arr[i].speed * dt;
        if (arr[i].radius > 175.0) { star_spawn(&arr[i]); arr[i].radius = 1.0; }
    }
}

/* ── Scope ─────────────────────────────────────────────────────────── */

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

    #define PT(px, py) x + (int)((px) * sx), y + (int)((py) * sy)
    draw_line(fb, PT(45,130), PT(30,145), COL_FG);
    draw_line(fb, PT(30,145), PT(30,315), COL_FG);
    draw_line(fb, PT(30,315), PT(45,330), COL_FG);
    draw_line(fb, PT(435,130), PT(450,145), COL_FG);
    draw_line(fb, PT(450,145), PT(450,315), COL_FG);
    draw_line(fb, PT(450,315), PT(435,330), COL_FG);
    draw_line(fb, PT(30,230), PT(37,230), COL_FG);
    draw_line(fb, PT(450,230), PT(443,230), COL_FG);

    draw_circle(fb, cx, cy, R, COL_FG);
    draw_ellipse(fb, cx, cy, (int)(280 * sx), (int)(130 * sy), R, COL_FG);
    draw_ellipse(fb, cx, cy, (int)(130 * sx), (int)(280 * sy), R, COL_FG);

    draw_line(fb, PT(48,230), PT(432,230), COL_FG);
    draw_line(fb, PT(240,42), PT(240,418), COL_FG);

    int ch = SC(12), cl = SC(4);
    draw_line(fb, cx-ch, cy, cx-cl, cy, COL_FG);
    draw_line(fb, cx+cl, cy, cx+ch, cy, COL_FG);
    draw_line(fb, cx, cy-ch, cx, cy-cl, COL_FG);
    draw_line(fb, cx, cy+cl, cx, cy+ch, COL_FG);
    #undef PT

    double max_r_px = 175.0 * unit;
    for (int i = 0; i < n_stars; i++) {
        double r_px = stars[i].radius * unit;
        if (r_px <= 0 || r_px > max_r_px) continue;
        double frac = stars[i].radius / 175.0;
        int sz = SC(1 + frac * 3.5);
        if (sz < 1) sz = 1;
        int px = cx + (int)(stars[i].cos_a * r_px);
        int py = cy + (int)(stars[i].sin_a * r_px);
        draw_rect(fb, px - sz / 2, py - sz / 2, sz, sz, COL_FG);
    }

    char l1[24], l2[24], l3[24];
    int net_active = 0;
    double sum = si->net_down_bps + si->net_up_bps;
    if (sum > 0) net_active = (int)(log10(1.0 + sum) * 12.0 + 0.5);
    if (net_active > 99) net_active = 99;
    snprintf(l1, sizeof(l1), "NET %02d", net_active);
    snprintf(l2, sizeof(l2), "CPU %03d", (int)(si->cpu_avg + 0.5));
    snprintf(l3, sizeof(l3), "GPU %03d", (int)(si->gpu_load_pct + 0.5));

    int rh   = font_line_height(F->body);
    int rw   = font_text_width(F->body, "GPU 999") + SC(14);
    int rx   = x + (int)(w * 0.62);
    int ry   = y + (int)(h * 0.20);
    int boxh  = 3 * rh + SC(6);
    draw_rect(fb, rx, ry, rw, boxh, COL_BG_FILL);
    draw_frame(fb, rx, ry, rw, boxh, COL_FG);
    txt(fb, F->body, rx + SC(7), ry + SC(3),          l1, COL_FG);
    txt(fb, F->body, rx + SC(7), ry + SC(3) + rh,     l2, COL_FG);
    txt(fb, F->body, rx + SC(7), ry + SC(3) + 2 * rh, l3, COL_FG);

    const char *p = "A U T O";
    int ppad = SC(9);
    int pw = font_text_width(F->body, p) + ppad * 2;
    int ph = rh + SC(4);
    int px = cx - pw / 2;
    int py = y + h - (int)(h * 0.09) - ph / 2;
    draw_rect(fb, px, py, pw, ph, COL_FG);
    txt_center(fb, F->body, cx, py + SC(2), p, COL_PILL_FG);
}

/* ── Composite frame (renders directly to the screen buffer, scaled) ── */

static void render(fb_t *fb, fonts_t *F,
                   const sysinfo_t *si, const audio_state_t *au,
                   const star_t *stars, int n_stars, double t_sec) {
    int sw = SC(HUD_W), sh = SC(HUD_H);
    int ox = (fb->w - sw) / 2; if (ox < 0) ox = 0;
    int oy = (fb->h - sh) / 2; if (oy < 0) oy = 0;

    /* Whole surface to the panel color (opaque, matches Qt useBackground). */
    draw_clear(fb, COL_BG_FILL);

    layout_t L = layout_compute();

    /* ── Top row ─────────────────────────────────────────────────── */
    char buf[64];
    fmt_clock_wall(buf, sizeof(buf));
    txt(fb, F->small, ox + SC(PAD_X), oy + SC(PAD_Y), "SYS TIME:", COL_FG);
    txt(fb, F->label, ox + SC(PAD_X), oy + SC(PAD_Y) + font_line_height(F->small), buf, COL_FG);

    int title_l = ox + SC(L.mid_x);
    int title_r = ox + SC(L.mid_x + L.mid_w);
    int title_w = font_text_width(F->label, "S Y S M O N");
    int title_cx = title_l + SC(L.mid_w) / 2;
    txt_center(fb, F->label, title_cx, oy + SC(PAD_Y + 4), "S Y S M O N", COL_FG);
    int tick_y = oy + SC(PAD_Y + 6);
    int n_ticks = 14;
    int gap_l = (title_cx - title_w / 2 - SC(8)) - title_l;
    int gap_r = title_r - (title_cx + title_w / 2 + SC(8));
    int step_l = gap_l / n_ticks; if (step_l < SC(6)) step_l = SC(6);
    int step_r = gap_r / n_ticks; if (step_r < SC(6)) step_r = SC(6);
    int tick_h = SC(9);
    for (int i = 0; i < n_ticks; i++) {
        uint32_t c = (i % 3 == 1) ? COL_FG_DIM : COL_FG;
        draw_vline(fb, title_l + i * step_l + SC(4),            tick_y, tick_h, c);
        draw_vline(fb, title_cx + title_w / 2 + SC(8) + i * step_r, tick_y, tick_h, c);
    }

    fmt_uptime(si->uptime_sec, buf, sizeof(buf));
    int rcol = ox + SC(L.right_x + RIGHT_W);
    txt_right(fb, F->small, rcol, oy + SC(PAD_Y), "UPTIME:", COL_FG);
    txt_right(fb, F->label, rcol, oy + SC(PAD_Y) + font_line_height(F->small), buf, COL_FG);

    /* ── Left column ─────────────────────────────────────────────── */
    int lx = ox + SC(PAD_X), lw = SC(LEFT_W);
    int y = oy + SC(L.mid_y);
    y = cpu_section  (fb, F, lx, y,           lw, si);
    y = mem_section  (fb, F, lx, y + SC(12),  lw, si);
    y = audio_section(fb, F, lx, y + SC(12),  lw, au);

    /* ── Right column ────────────────────────────────────────────── */
    int rx = ox + SC(L.right_x), rw = SC(RIGHT_W);
    int yr = oy + SC(L.mid_y);
    if (si->has_gpu) yr = gpu_section(fb, F, rx, yr, rw, si);
    yr = therm_section(fb, F, rx, yr + SC(12), rw, si);
    if (si->has_battery) yr = batt_section(fb, F, rx, yr + SC(12), rw, si);
    yr = disk_section (fb, F, rx, yr + SC(12), rw, si);
    yr = net_section  (fb, F, rx, yr + SC(12), rw, si);
    if (si->has_brightness) yr = display_section(fb, F, rx, yr + SC(12), rw, si);

    /* ── Center: YearStack | Scope | PitchStack ──────────────────── */
    {
        int my = oy + SC(L.mid_y);
        int mh = SC(L.mid_row_h);
        int stack_w = SC(70);
        int mid_x_px = ox + SC(L.mid_x);
        int mid_w_px = SC(L.mid_w);

        year_stack (fb, F, mid_x_px, my, stack_w, mh);
        pitch_stack(fb, F, mid_x_px + mid_w_px - stack_w, my, stack_w, mh, t_sec);
        scope_render(fb, F, mid_x_px + stack_w, my,
                     mid_w_px - stack_w * 2, mh, stars, n_stars, si);
    }

    /* ── Bottom row ──────────────────────────────────────────────── */
    int by = oy + SC(L.bot_y) + (SC(BOT_H) - font_line_height(F->body)) / 2;
    {
        int pad = SC(8);
        const char *p = "SYSMON";
        int pw = font_text_width(F->body, p) + pad * 2;
        int ph = font_line_height(F->body) + SC(4);
        draw_rect(fb, lx, by - SC(2), pw, ph, COL_FG);
        txt(fb, F->body, lx + pad, by - SC(2), p, COL_PILL_FG);
    }
    {
        const char *k[4] = { "HOST:", "KERNEL:", "SHELL:", "USER:" };
        const char *v[4] = { si->host, si->kernel, si->shell, si->user };
        int gap = SC(26);
        int total = 0;
        for (int i = 0; i < 4; i++)
            total += font_text_width(F->body, k[i]) + SC(4) +
                     font_text_width(F->body, v[i]);
        total += gap * 3;
        int cx = ox + SC(L.mid_x) + SC(L.mid_w) / 2 - total / 2;
        for (int i = 0; i < 4; i++)
            cx += kv_inline(fb, F, cx, by, k[i], v[i]) + gap;
    }
    {
        txt_right(fb, F->small, rcol, by - font_line_height(F->small) + SC(2),
                  "LOAD AVG:", COL_FG);
        char la[64];
        snprintf(la, sizeof(la), "%.2f %.2f %.2f",
                 si->load_avg[0], si->load_avg[1], si->load_avg[2]);
        txt_right(fb, F->body, rcol, by, la, COL_FG);
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

static uint32_t parse_color(const char *env, uint32_t def) {
    const char *s = getenv(env);
    if (!s || s[0] != '#') return def;
    char *end; uint32_t v = strtoul(s + 1, &end, 16);
    ptrdiff_t n = end - (s + 1);
    if (n == 6) return 0xFF000000u | v;
    if (n == 8) return v;
    return def;
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

    /* Position — default fullscreen (Qt-style scaled overlay). Corners use a
     * native-size window (scale 1). */
    unsigned anchors = WL_ANCHOR_ALL;
    const char *pos = getenv("DEPARTURE_HUD_POSITION");
    if (pos) {
        if      (!strcmp(pos, "fullscreen"))   anchors = WL_ANCHOR_ALL;
        else if (!strcmp(pos, "center"))       anchors = 0;
        else if (!strcmp(pos, "top-left"))     anchors = WL_ANCHOR_TOP    | WL_ANCHOR_LEFT;
        else if (!strcmp(pos, "top-right"))    anchors = WL_ANCHOR_TOP    | WL_ANCHOR_RIGHT;
        else if (!strcmp(pos, "bottom-left"))  anchors = WL_ANCHOR_BOTTOM | WL_ANCHOR_LEFT;
        else if (!strcmp(pos, "bottom-right")) anchors = WL_ANCHOR_BOTTOM | WL_ANCHOR_RIGHT;
    }
    /* Layer — default BOTTOM: desktop widget above wallpaper, below windows. */
    wl_layer_t layer = WL_LAYER_BOTTOM;
    const char *ls = getenv("DEPARTURE_HUD_LAYER");
    if (ls) {
        if      (!strcmp(ls, "overlay"))    layer = WL_LAYER_OVERLAY;
        else if (!strcmp(ls, "top"))        layer = WL_LAYER_TOP;
        else if (!strcmp(ls, "bottom"))     layer = WL_LAYER_BOTTOM;
        else if (!strcmp(ls, "background")) layer = WL_LAYER_BACKGROUND;
    }

    /* Colors. */
    COL_FG      = parse_color("DEPARTURE_HUD_ACCENT", COL_FG);
    COL_HOT     = parse_color("DEPARTURE_HUD_HOT",    COL_HOT);
    COL_BG_FILL = parse_color("DEPARTURE_HUD_BG",     COL_BG_FILL);
    {
        uint8_t a = (COL_FG >> 24) & 0xFF, r = (COL_FG >> 16) & 0xFF;
        uint8_t g = (COL_FG >> 8) & 0xFF,  b = COL_FG & 0xFF;
        COL_FG_DIM  = ((uint32_t)a<<24)|((uint32_t)(r/2.4)<<16)|((uint32_t)(g/2.4)<<8)|(uint32_t)(b/2.4);
        COL_FG_SOFT = ((uint32_t)a<<24)|((uint32_t)(r/6.0)<<16)|((uint32_t)(g/6.0)<<8)|(uint32_t)(b/6.0);
    }

    wl_window_opts_t opts = {
        .width = HUD_W, .height = HUD_H,
        .layer = layer,
        .anchors = anchors,
        .click_through = true,
        .namespace_ = "departure-hud",
        .output_name = getenv("DEPARTURE_HUD_OUTPUT"),  /* NULL → compositor picks */
    };
    wl_ctx_t *wl = wl_open(&opts);
    if (!wl) return 1;

    /* Scale: by default fit the 1180×600 design into the output preserving
     * aspect ratio (Qt's min(W/1180, H/600)) and letterbox the rest.
     * DEPARTURE_HUD_SCALE overrides with an absolute factor (1.0 = native
     * design size, 2.0 = double, …). */
    fb_t fb0 = wl_framebuffer(wl);
    const char *scale_env = getenv("DEPARTURE_HUD_SCALE");
    if (scale_env && *scale_env)
        g_scale = strtod(scale_env, NULL);
    else
        g_scale = fmin((double)fb0.w / HUD_W, (double)fb0.h / HUD_H);
    if (g_scale < 0.3) g_scale = 0.3;
    if (g_scale > 6.0) g_scale = 6.0;

    fonts_t F = {
        .small = font_open(fp, SC(F_SMALL)),
        .body  = font_open(fp, SC(F_BODY)),
        .label = font_open(fp, SC(F_LABEL)),
    };
    if (!F.small || !F.body || !F.label) {
        fprintf(stderr, "departure-hud-mini: cannot load font at %s\n", fp);
        return 1;
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (tfd < 0) { perror("timerfd"); return 1; }
    struct itimerspec it = {
        .it_value    = { 0, 100 * 1000 * 1000 },
        .it_interval = { 0, 100 * 1000 * 1000 },
    };
    timerfd_settime(tfd, 0, &it, NULL);

    /* Disk list. */
    static const char *disks_arr[SYS_MAX_DISKS + 1];
    static char        disks_buf[512];
    int disks_n = 0;
    {
        const char *raw = getenv("DEPARTURE_HUD_DISKS");
        if (raw && *raw) {
            snprintf(disks_buf, sizeof(disks_buf), "%s", raw);
            char *save = NULL;
            for (char *tok = strtok_r(disks_buf, ",", &save);
                 tok && disks_n < SYS_MAX_DISKS;
                 tok = strtok_r(NULL, ",", &save)) {
                while (*tok == ' ') tok++;
                size_t l = strlen(tok);
                while (l > 0 && tok[l - 1] == ' ') tok[--l] = '\0';
                if (*tok) disks_arr[disks_n++] = tok;
            }
            disks_arr[disks_n] = NULL;
        }
    }
    sys_opts_t sopts = { 0 };
    if (disks_n) sopts.disks_to_show = disks_arr;
    sys_init(&sopts);
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

/* Departure HUD — minimal Wayland edition.
 *
 * Renders the same dashboard as the Qt build directly into a single shm
 * buffer: layer-shell fullscreen overlay, click-through, software-rasterised
 * text and bars. No QML, no scene graph, no Mesa, no NVIDIA libs at runtime
 * unless the user has nvidia-ml installed (in which case we dlopen it for
 * GPU stats). */

#include "wl.h"
#include "draw.h"
#include "font.h"
#include "sys.h"

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
 * of blacking out the whole screen. Section "fill" colors keep their alpha
 * so we can drop slightly-darkened panels behind dense content if needed. */
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

/* Font sizes (px). */
#define F_SMALL 9
#define F_BODY  11
#define F_LABEL 13
#define F_TITLE 16

typedef struct {
    font_t *small;
    font_t *body;
    font_t *label;
    font_t *title;
} fonts_t;

/* ── Geometry helpers ───────────────────────────────────────────────── */

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

/* ── Drawing primitives shared across sections ──────────────────────── */

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

/* Section header — horizontal rule with little ticks hanging *down* at the
 * ends and the label sitting on the line. Returns the y-coordinate where
 * the section body should start. */
static int section_header(fb_t *fb, fonts_t *F, int x, int y, int w,
                          const char *label) {
    int lw    = font_text_width(F->label, label);
    int pad   = font_cell_width(F->label);
    int gap_w = lw + pad * 2;
    int gap_x = x + (w - gap_w) / 2;
    /* Horizontal line drawn in two halves so the label gets a clean break. */
    draw_hline(fb, x,           y, gap_x - x,                 COL_FG);
    draw_hline(fb, gap_x + gap_w, y, x + w - (gap_x + gap_w), COL_FG);
    /* Down-pointing ticks at both ends. */
    draw_vline(fb, x,         y, 7, COL_FG);
    draw_vline(fb, x + w - 1, y, 7, COL_FG);
    /* Label sits centered on (where) the line would be. */
    int box_h = font_line_height(F->label);
    font_draw(fb, F->label, gap_x + pad,
              y - box_h / 2 + font_ascent(F->label), label, COL_FG);
    return y + 10;
}

/* Bar row: filled progress + right-aligned value text. */
static int bar_row(fb_t *fb, fonts_t *F, int x, int y, int w,
                   double pct, const char *value, bool hot) {
    int val_w = font_text_width(F->body, value);
    int gap   = font_cell_width(F->body);
    int bar_w = w - val_w - gap;
    int bar_h = 8;
    int bar_y = y + (font_line_height(F->body) - bar_h) / 2;

    draw_rect(fb, x, bar_y, bar_w, bar_h, COL_FG_SOFT);
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    int fill = (int)(bar_w * pct / 100.0 + 0.5);
    if (fill > 0) {
        draw_rect(fb, x, bar_y, fill, bar_h, hot ? COL_HOT : COL_FG);
        if (fill > 3) draw_vline(fb, x + fill - 3, bar_y, bar_h, COL_BG);
    }
    txt(fb, F->body, x + bar_w + gap, y, value, hot ? COL_HOT : COL_FG);
    return y + font_line_height(F->body) + 2;
}

/* Two-column "key over value" stack inside a column. */
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

/* Inline "key: value" pair — used in the bottom info row. Returns x advance. */
static int kv_inline(fb_t *fb, fonts_t *F, int x, int y,
                     const char *k, const char *v) {
    txt(fb, F->body, x, y, k, COL_FG);
    int kw = font_text_width(F->body, k);
    txt(fb, F->body, x + kw + 4, y, v, COL_FG);
    int vw = font_text_width(F->body, v);
    return kw + 4 + vw;
}

/* ── Formatting helpers ─────────────────────────────────────────────── */

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

static void fmt_clock(unsigned long uptime_sec, char *out, size_t cap,
                      bool wall_clock) {
    if (wall_clock) {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        snprintf(out, cap, "%02d:%02d:%02d",
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        unsigned d = uptime_sec / 86400;
        unsigned h = (uptime_sec % 86400) / 3600;
        unsigned m = (uptime_sec % 3600) / 60;
        unsigned s = uptime_sec % 60;
        snprintf(out, cap, "%02uD %02u:%02u:%02u", d, h, m, s);
    }
}

/* ── Section renderers ──────────────────────────────────────────────── */

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
    int col_k = x, col_v = x + 70, col_m = x + w - 28;
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        if (!rows[i].has) continue;
        txt(fb, F->body, col_k, y, rows[i].k, COL_FG);
        char v[16]; snprintf(v, sizeof(v), "%d°C", (int)(rows[i].v + 0.5));
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
    kv_block(fb, F, x + half+10,y2, half, "RATE", rate, true);

    int y3 = y2 + font_line_height(F->small) + font_line_height(F->body) + 4;
    char tl[16];
    snprintf(tl, sizeof(tl), "%02d:%02d",
             si->bat_minutes_left / 60, si->bat_minutes_left % 60);
    kv_block(fb, F, x,          y3, half, "TIME LEFT", tl, false);
    char cyc[16]; snprintf(cyc, sizeof(cyc), "%d", si->bat_cycles);
    kv_block(fb, F, x + half+10,y3, half, "CYCLES",    cyc, true);
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
    char hdr[80];
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

/* ── Composite frame ────────────────────────────────────────────────── */

static void render(fb_t *fb, fonts_t *F, const sysinfo_t *si) {
    /* Compute centered HUD origin, in case the window is larger than the
     * design surface (overlay covers the whole screen). */
    int ox = (fb->w - HUD_W) / 2; if (ox < 0) ox = 0;
    int oy = (fb->h - HUD_H) / 2; if (oy < 0) oy = 0;

    /* Only the HUD bounding rect needs to be wiped between frames — the rest
     * of the framebuffer stays at its mmap-initialized 0x00000000 (fully
     * transparent) so the user sees their desktop through it. Costs ~2.7 MB
     * per frame vs. ~8 MB if we cleared the whole 1920x1080 surface. */
    draw_rect(fb, ox, oy, HUD_W, HUD_H, COL_BG);

    layout_t L = layout_compute();
    /* Translate everything by (ox, oy). We do that by passing pre-offset
     * coords into the section renderers — simplest, no transform stack. */

    /* ── Top row ────────────────────────────────────────────────── */
    char buf[64];
    fmt_clock(0, buf, sizeof(buf), true);
    txt(fb, F->small, ox + PAD_X, oy + PAD_Y,                       "SYS TIME:",     COL_FG);
    txt(fb, F->label, ox + PAD_X, oy + PAD_Y + font_line_height(F->small), buf, COL_FG);

    /* Center title with tick rows on either side. */
    int title_x = ox + L.mid_x;
    txt_center(fb, F->label, title_x + L.mid_w / 2, oy + PAD_Y + 4,
               "S Y S M O N", COL_FG);
    /* Tick rows: '|' chars on both sides of title. */
    int tick_y = oy + PAD_Y + 6;
    int tx0 = title_x;
    int tx1 = title_x + L.mid_w;
    int title_w_px = font_text_width(F->label, "S Y S M O N");
    int title_l = title_x + L.mid_w / 2 - title_w_px / 2;
    int title_r = title_l + title_w_px;
    /* draw 14 ticks on each side */
    int n_ticks = 14, step;
    step = (title_l - 8 - tx0) / n_ticks; if (step < 6) step = 6;
    for (int i = 0; i < n_ticks; i++) {
        uint32_t c = (i % 3 == 1) ? COL_FG_DIM : COL_FG;
        draw_vline(fb, tx0 + i * step + 4, tick_y, 9, c);
    }
    step = (tx1 - title_r - 8) / n_ticks; if (step < 6) step = 6;
    for (int i = 0; i < n_ticks; i++) {
        uint32_t c = (i % 3 == 1) ? COL_FG_DIM : COL_FG;
        draw_vline(fb, title_r + 8 + i * step, tick_y, 9, c);
    }

    fmt_clock(si->uptime_sec, buf, sizeof(buf), false);
    txt_right(fb, F->small, ox + L.right_x + RIGHT_W, oy + PAD_Y,
              "UPTIME:", COL_FG);
    txt_right(fb, F->label, ox + L.right_x + RIGHT_W,
              oy + PAD_Y + font_line_height(F->small), buf, COL_FG);

    /* ── Middle: left column ────────────────────────────────────── */
    int y = oy + L.mid_y;
    y = cpu_section(fb, F, ox + PAD_X, y,             LEFT_W, si);
    y = mem_section(fb, F, ox + PAD_X, y + 12,        LEFT_W, si);

    /* ── Middle: right column ───────────────────────────────────── */
    int yr = oy + L.mid_y;
    if (si->has_gpu) yr = gpu_section(fb, F, ox + L.right_x, yr,         RIGHT_W, si);
    yr = therm_section(fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    if (si->has_battery)
        yr = batt_section(fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    yr = disk_section (fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    yr = net_section  (fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);
    if (si->has_brightness)
        yr = display_section(fb, F, ox + L.right_x, yr + 12, RIGHT_W, si);

    /* ── Middle: scope placeholder (visual stuff lands in iter 3) ── */
    {
        int sx = ox + L.mid_x + 70;
        int sy = oy + L.mid_y + 20;
        int sw = L.mid_w - 140;
        int sh = L.mid_row_h - 40;
        draw_frame(fb, sx, sy, sw, sh, COL_FG_DIM);
        txt_center(fb, F->small, sx + sw / 2, sy + sh / 2 - 6,
                   "[ SCOPE — iter 3 ]", COL_FG_DIM);
    }

    /* ── Bottom row ─────────────────────────────────────────────── */
    int by = oy + L.bot_y + (BOT_H - font_line_height(F->body)) / 2;

    /* Left pill: SYSMON. */
    {
        int pad = 8;
        const char *p = "SYSMON";
        int w = font_text_width(F->body, p) + pad * 2;
        int h = font_line_height(F->body);
        int px = ox + PAD_X;
        draw_rect(fb, px, by - 2, w, h + 4, COL_FG);
        txt(fb, F->body, px + pad, by - 2, p, COL_PILL_FG);
    }

    /* Center inline: HOST / KERNEL / SHELL / USER. */
    {
        char hostv[80], kerv[80], shv[64], userv[64];
        snprintf(hostv, sizeof(hostv), "%s", si->host);
        snprintf(kerv,  sizeof(kerv),  "%s", si->kernel);
        snprintf(shv,   sizeof(shv),   "%s", si->shell);
        snprintf(userv, sizeof(userv), "%s", si->user);

        /* Compute total width and center. */
        int gap = 26;
        int kw[4]; const char *k[4] = { "HOST:", "KERNEL:", "SHELL:", "USER:" };
        const char *v[4] = { hostv, kerv, shv, userv };
        int total = 0;
        for (int i = 0; i < 4; i++) {
            kw[i] = font_text_width(F->body, k[i]) + 4 +
                    font_text_width(F->body, v[i]);
            total += kw[i];
        }
        total += gap * 3;
        int cx = ox + L.mid_x + L.mid_w / 2 - total / 2;
        for (int i = 0; i < 4; i++) {
            cx += kv_inline(fb, F, cx, by, k[i], v[i]) + gap;
        }
    }

    /* Right: LOAD AVG. */
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
        .title = font_open(fp, F_TITLE),
    };
    if (!F.small || !F.body || !F.label || !F.title) {
        fprintf(stderr, "departure-hud-mini: cannot load font at %s\n", fp);
        return 1;
    }

    wl_window_opts_t opts = {
        .width = HUD_W, .height = HUD_H,
        .layer = WL_LAYER_OVERLAY,
        .anchors = WL_ANCHOR_ALL,
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
    sysinfo_t si = {0};
    sys_poll_fast(&si);
    sys_poll_slow(&si);
    sys_poll_gpu (&si);

    /* First frame. */
    {
        fb_t fb = wl_framebuffer(wl);
        render(&fb, &F, &si);
        wl_commit(wl);
    }

    struct pollfd fds[2] = {
        { .fd = wl_fd(wl), .events = POLLIN },
        { .fd = tfd,       .events = POLLIN },
    };

    /* Slow-poll cadence: every 10th fast tick (~1 s). */
    int slow_counter = 0;
    bool need_redraw = false;

    while (!wl_should_close(wl)) {
        int r = poll(fds, 2, -1);
        if (r < 0) { if (errno == EINTR) continue; perror("poll"); break; }

        if (fds[0].revents & POLLIN) wl_pump(wl);
        if (fds[1].revents & POLLIN) {
            uint64_t ticks; (void)!read(tfd, &ticks, sizeof(ticks));
            sys_poll_fast(&si);
            if (++slow_counter >= 10) {
                slow_counter = 0;
                sys_poll_slow(&si);
                sys_poll_gpu (&si);
            }
            need_redraw = true;
        }
        if (need_redraw && wl_frame_ready(wl)) {
            fb_t fb = wl_framebuffer(wl);
            render(&fb, &F, &si);
            wl_commit(wl);
            need_redraw = false;
        }
    }

    close(tfd);
    wl_close(wl);
    font_close(F.title); font_close(F.label);
    font_close(F.body);  font_close(F.small);
    return 0;
}

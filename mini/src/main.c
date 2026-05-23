/* Departure HUD — minimal Wayland edition.
 *
 * MVP scope (iteration 1):
 *   • single layer-shell surface, fixed size
 *   • bundled Departure Mono font, software-rendered text
 *   • two bar rows wired to /proc/stat (CPU%) and /proc/meminfo (MEM%)
 *   • 10 Hz redraw loop, paced by Wayland frame callbacks + timerfd
 *
 * Everything is written to a single shm buffer; no GPU, no scene graph. */

#include "wl.h"
#include "draw.h"
#include "font.h"
#include "sys.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define WIN_W 720
#define WIN_H 220

#define COL_BG      rgba(0x0d, 0x0d, 0x0d, 0xFF)
#define COL_FG      rgba(0xf0, 0x8a, 0x28, 0xFF)
#define COL_FG_SOFT rgba(0x35, 0x1d, 0x09, 0xFF)
#define COL_HOT     rgba(0xff, 0x5a, 0x3c, 0xFF)

/* ── Layout helpers ─────────────────────────────────────────────────── */

/* Section header: a horizontal rule with two short ticks hanging down at the
 * ends, label centered on the line — the same "bracket" affordance as the Qt
 * version. Returns the y-coordinate just below the bracket. */
static int draw_section_header(fb_t *fb, const font_t *f, int x, int y,
                               int w, const char *label) {
    draw_hline(fb, x, y, w, COL_FG);
    draw_vline(fb, x,           y, 7, COL_FG);
    draw_vline(fb, x + w - 1,   y, 7, COL_FG);

    int lw      = font_text_width(f, label);
    int box_pad = font_cell_width(f);
    int box_w   = lw + box_pad * 2;
    int box_x   = x + (w - box_w) / 2;
    int asc     = font_ascent(f);
    int box_h   = font_line_height(f);
    int box_y   = y - box_h / 2;

    draw_rect(fb, box_x, box_y, box_w, box_h, COL_BG);
    font_draw(fb, f, box_x + box_pad, box_y + asc, label, COL_FG);
    return y + 10;
}

/* One labelled progress bar:
 *   <bar fill>  <right-aligned value>
 * Returns the next y. */
static int draw_bar_row(fb_t *fb, const font_t *f, int x, int y, int w,
                        double pct, const char *value, bool hot) {
    int val_w   = font_text_width(f, value);
    int gap     = font_cell_width(f);
    int bar_w   = w - val_w - gap;
    int bar_h   = font_line_height(f) - 4;
    int bar_y   = y + (font_line_height(f) - bar_h) / 2;

    /* Background trough. */
    draw_rect(fb, x, bar_y, bar_w, bar_h, COL_FG_SOFT);

    /* Filled portion. */
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    int fill_w = (int)(bar_w * pct / 100.0 + 0.5);
    if (fill_w > 0) {
        draw_rect(fb, x, bar_y, fill_w, bar_h, hot ? COL_HOT : COL_FG);
        /* Notch at the end of the fill so it reads like a bar, not a block. */
        if (fill_w > 3) draw_vline(fb, x + fill_w - 3, bar_y, bar_h, COL_BG);
    }

    font_draw(fb, f, x + bar_w + gap, y + font_ascent(f),
              value, hot ? COL_HOT : COL_FG);
    return y + font_line_height(f) + 2;
}

/* ── Frame composer ─────────────────────────────────────────────────── */

static void render(fb_t *fb, const font_t *title_font, const font_t *body_font,
                   const sysinfo_t *si) {
    draw_clear(fb, COL_BG);

    int pad = 14;
    int x   = pad;
    int y   = pad;
    int w   = fb->w - pad * 2;

    /* Top title bar. */
    int tw = font_text_width(title_font, "DEPARTURE-HUD MINI");
    font_draw(fb, title_font, x + (w - tw) / 2,
              y + font_ascent(title_font),
              "DEPARTURE-HUD MINI", COL_FG);
    y += font_line_height(title_font) + 10;

    /* CPU section. */
    y = draw_section_header(fb, body_font, x, y, w, "CPU");
    char val[32];
    snprintf(val, sizeof(val), "%3.0f%%", si->cpu_avg);
    y = draw_bar_row(fb, body_font, x, y, w, si->cpu_avg, val, si->cpu_avg > 88);

    /* Per-core mini bars (up to 8). */
    int show = si->cpu_n < 8 ? si->cpu_n : 8;
    for (int i = 0; i < show; i++) {
        snprintf(val, sizeof(val), "C%-2d %3.0f%%", i, si->cpu_per[i]);
        y = draw_bar_row(fb, body_font, x, y, w,
                         si->cpu_per[i], val, si->cpu_per[i] > 88);
    }

    /* MEM section. */
    y += 6;
    y = draw_section_header(fb, body_font, x, y, w, "MEM");
    snprintf(val, sizeof(val), "%4.1fG", si->mem_used_gb);
    y = draw_bar_row(fb, body_font, x, y, w,
                     si->mem_used_pct, val, si->mem_used_pct > 90);
    if (si->swap_used_gb > 0.01) {
        snprintf(val, sizeof(val), "S %3.1fG", si->swap_used_gb);
        y = draw_bar_row(fb, body_font, x, y, w,
                         si->swap_used_pct, val, false);
    }
}

/* ── Locate the bundled font ────────────────────────────────────────── */

static const char *find_font(void) {
    static const char *candidates[] = {
        "fonts/DepartureMono-Regular.otf",        /* run from mini/ */
        "../fonts/DepartureMono-Regular.otf",     /* run from mini/build/ */
        "/usr/share/departure-hud/fonts/DepartureMono-Regular.otf",
        NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], R_OK) == 0) return candidates[i];
    }
    return NULL;
}

/* ── Main loop ──────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    const char *font_path = find_font();
    if (!font_path) {
        fprintf(stderr,
            "departure-hud-mini: cannot find DepartureMono-Regular.otf\n"
            "  Tried ./fonts, ../fonts, /usr/share/departure-hud/fonts.\n"
            "  Run from the project root or symlink the font into ./fonts/.\n");
        return 1;
    }
    font_t *body  = font_open(font_path, 13);
    font_t *title = font_open(font_path, 16);
    if (!body || !title) {
        fprintf(stderr, "departure-hud-mini: failed to load font at %s\n", font_path);
        return 1;
    }

    wl_window_opts_t opts = {
        .width = WIN_W, .height = WIN_H,
        .layer = WL_LAYER_OVERLAY, .namespace_ = "departure-hud",
    };
    wl_ctx_t *wl = wl_open(&opts);
    if (!wl) return 1;

    /* 10 Hz redraw timer. The frame callback throttles us if the compositor
     * skips frames; this just provides a steady wake-up. */
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (tfd < 0) { perror("timerfd_create"); return 1; }
    struct itimerspec it = {
        .it_value    = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 },
        .it_interval = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 },
    };
    timerfd_settime(tfd, 0, &it, NULL);

    sys_init();
    sysinfo_t si = {0};
    sys_poll(&si);   /* prime CPU counters */

    /* First frame so the compositor maps the surface. */
    {
        fb_t fb = wl_framebuffer(wl);
        render(&fb, title, body, &si);
        wl_commit(wl);
    }

    struct pollfd fds[2] = {
        { .fd = wl_fd(wl), .events = POLLIN },
        { .fd = tfd,       .events = POLLIN },
    };

    bool need_redraw = false;

    while (!wl_should_close(wl)) {
        int r = poll(fds, 2, -1);
        if (r < 0) { if (errno == EINTR) continue; perror("poll"); break; }

        if (fds[0].revents & POLLIN) wl_pump(wl);

        if (fds[1].revents & POLLIN) {
            uint64_t ticks; (void)!read(tfd, &ticks, sizeof(ticks));
            sys_poll(&si);
            need_redraw = true;
        }

        if (need_redraw && wl_frame_ready(wl)) {
            fb_t fb = wl_framebuffer(wl);
            render(&fb, title, body, &si);
            wl_commit(wl);
            need_redraw = false;
        }
    }

    close(tfd);
    wl_close(wl);
    font_close(body);
    font_close(title);
    return 0;
}

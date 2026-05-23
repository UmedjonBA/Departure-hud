#pragma once
#include "draw.h"
#include <stdbool.h>

typedef struct wl_ctx wl_ctx_t;

typedef enum {
    WL_LAYER_BACKGROUND,
    WL_LAYER_BOTTOM,
    WL_LAYER_TOP,
    WL_LAYER_OVERLAY,
} wl_layer_t;

/* Anchor mask — combine to pin the surface to compositor edges. With all
 * four set, layer-shell makes the surface the size of the output and ignores
 * the explicit width/height. */
enum {
    WL_ANCHOR_TOP    = 1 << 0,
    WL_ANCHOR_BOTTOM = 1 << 1,
    WL_ANCHOR_LEFT   = 1 << 2,
    WL_ANCHOR_RIGHT  = 1 << 3,
    WL_ANCHOR_ALL    = 0xF,
};

typedef struct {
    int        width;
    int        height;
    wl_layer_t layer;
    unsigned   anchors;          /* OR of WL_ANCHOR_*. 0 → centered by compositor. */
    bool       click_through;    /* empty input region; mouse passes through. */
    const char *namespace_;      /* layer-shell scope name, e.g. "departure-hud" */
    const char *output_name;     /* connector name (e.g. "DP-1"); NULL → compositor picks */
} wl_window_opts_t;

/* Connect to the compositor, create the surface, allocate the shm buffer.
 * Returns NULL on any failure (and prints to stderr). */
wl_ctx_t *wl_open(const wl_window_opts_t *opts);
void      wl_close(wl_ctx_t *c);

/* Hand the application the pixel buffer to draw into. The pointer is valid
 * until the next wl_commit() or wl_close(). */
fb_t      wl_framebuffer(wl_ctx_t *c);

/* Submit the current framebuffer for display. Marks the whole surface damaged
 * and requests a new frame callback so the app can pace its redraws. */
void      wl_commit(wl_ctx_t *c);

/* True when the user/compositor asked the surface to close. */
bool      wl_should_close(const wl_ctx_t *c);

/* Returns the wayland-client file descriptor (for poll/epoll integration). */
int       wl_fd(const wl_ctx_t *c);

/* Reads any pending events without blocking. Returns true if it dispatched
 * at least one event. */
bool      wl_pump(wl_ctx_t *c);

/* True once the compositor has acknowledged at least one frame — i.e. it's
 * safe to call wl_commit() with the next snapshot. The first commit happens
 * inside wl_open(). */
bool      wl_frame_ready(const wl_ctx_t *c);

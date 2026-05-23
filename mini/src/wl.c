#define _GNU_SOURCE
#include "wl.h"
#include "gen/xdg-shell.h"
#include "gen/wlr-layer-shell-unstable-v1.h"

#include <sys/mman.h>

#include <wayland-client.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct wl_ctx {
    struct wl_display        *display;
    struct wl_registry       *registry;
    struct wl_compositor     *compositor;
    struct wl_shm            *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface          *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    /* Single shm pool/buffer. We re-use the same memory across frames. */
    int        shm_fd;
    uint32_t  *pixels;
    size_t     pixels_size;
    int        width, height, stride_px;
    struct wl_buffer *buffer;

    bool       configured;     /* received first layer_surface.configure */
    bool       frame_ready;    /* compositor wants the next frame */
    bool       should_close;
    int        pending_w, pending_h;
};

/* ── Wayland callbacks ──────────────────────────────────────────────── */

static void registry_global(void *data, struct wl_registry *r, uint32_t id,
                            const char *iface, uint32_t ver) {
    wl_ctx_t *c = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        c->compositor = wl_registry_bind(r, id, &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        c->shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        c->layer_shell = wl_registry_bind(r, id,
                                          &zwlr_layer_shell_v1_interface,
                                          ver < 4 ? ver : 4);
    }
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t id) {}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global, .global_remove = registry_remove,
};

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                            uint32_t serial, uint32_t w, uint32_t h) {
    wl_ctx_t *c = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w > 0) c->pending_w = w;
    if (h > 0) c->pending_h = h;
    c->configured  = true;
    c->frame_ready = true;
}
static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    wl_ctx_t *c = data;
    c->should_close = true;
}
static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure, .closed = layer_closed,
};

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    wl_ctx_t *c = data;
    wl_callback_destroy(cb);
    c->frame_ready = true;
}
static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

/* ── SHM buffer ─────────────────────────────────────────────────────── */

/* Allocate an anonymous, in-memory file with `size` bytes. Linux's memfd is
 * the cleanest path; everywhere else we fall back to an unlinked /tmp file. */
static int anon_shm(size_t size) {
    int fd = memfd_create("departure-hud-mini", MFD_CLOEXEC);
    if (fd < 0) {
        char name[] = "/tmp/dep-hud-XXXXXX";
        fd = mkstemp(name);
        if (fd < 0) return -1;
        unlink(name);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

static bool alloc_buffer(wl_ctx_t *c, int w, int h) {
    if (c->buffer && c->width == w && c->height == h) return true;

    if (c->buffer) { wl_buffer_destroy(c->buffer); c->buffer = NULL; }
    if (c->pixels) { munmap(c->pixels, c->pixels_size); c->pixels = NULL; }
    if (c->shm_fd >= 0) { close(c->shm_fd); c->shm_fd = -1; }

    int stride = w * 4;
    size_t size = (size_t)stride * h;
    int fd = anon_shm(size);
    if (fd < 0) { perror("shm_alloc"); return false; }

    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); close(fd); return false; }

    struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, size);
    c->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                          WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    c->shm_fd      = fd;
    c->pixels      = map;
    c->pixels_size = size;
    c->width       = w;
    c->height      = h;
    c->stride_px   = stride / 4;
    return true;
}

/* ── Public API ─────────────────────────────────────────────────────── */

wl_ctx_t *wl_open(const wl_window_opts_t *opts) {
    wl_ctx_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->shm_fd    = -1;
    c->pending_w = opts->width;
    c->pending_h = opts->height;

    c->display = wl_display_connect(NULL);
    if (!c->display) {
        fprintf(stderr, "departure-hud-mini: cannot connect to Wayland display\n");
        free(c); return NULL;
    }

    c->registry = wl_display_get_registry(c->display);
    wl_registry_add_listener(c->registry, &registry_listener, c);
    wl_display_roundtrip(c->display);

    if (!c->compositor || !c->shm) {
        fprintf(stderr, "departure-hud-mini: compositor missing wl_compositor / wl_shm\n");
        wl_close(c); return NULL;
    }
    if (!c->layer_shell) {
        fprintf(stderr, "departure-hud-mini: compositor does not advertise wlr-layer-shell-v1\n");
        wl_close(c); return NULL;
    }

    c->surface = wl_compositor_create_surface(c->compositor);
    if (!c->surface) { wl_close(c); return NULL; }

    enum zwlr_layer_shell_v1_layer layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    switch (opts->layer) {
        case WL_LAYER_BACKGROUND: layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND; break;
        case WL_LAYER_BOTTOM:     layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;     break;
        case WL_LAYER_TOP:        layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;        break;
        case WL_LAYER_OVERLAY:    layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;    break;
    }
    c->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        c->layer_shell, c->surface, NULL, layer,
        opts->namespace_ ? opts->namespace_ : "departure-hud-mini");
    zwlr_layer_surface_v1_add_listener(c->layer_surface, &layer_listener, c);
    zwlr_layer_surface_v1_set_size(c->layer_surface, opts->width, opts->height);
    /* No anchors → compositor centers us. Keyboard/pointer disabled — we're
     * a passive overlay. */
    zwlr_layer_surface_v1_set_keyboard_interactivity(c->layer_surface, 0);
    wl_surface_commit(c->surface);

    /* Block until first configure so we know our size and have the GO signal. */
    while (!c->configured && wl_display_dispatch(c->display) != -1) {}

    if (!alloc_buffer(c, c->pending_w, c->pending_h)) {
        wl_close(c); return NULL;
    }

    return c;
}

void wl_close(wl_ctx_t *c) {
    if (!c) return;
    if (c->buffer)        wl_buffer_destroy(c->buffer);
    if (c->pixels)        munmap(c->pixels, c->pixels_size);
    if (c->shm_fd >= 0)   close(c->shm_fd);
    if (c->layer_surface) zwlr_layer_surface_v1_destroy(c->layer_surface);
    if (c->surface)       wl_surface_destroy(c->surface);
    if (c->layer_shell)   zwlr_layer_shell_v1_destroy(c->layer_shell);
    if (c->shm)           wl_shm_destroy(c->shm);
    if (c->compositor)    wl_compositor_destroy(c->compositor);
    if (c->registry)      wl_registry_destroy(c->registry);
    if (c->display)       wl_display_disconnect(c->display);
    free(c);
}

fb_t wl_framebuffer(wl_ctx_t *c) {
    /* Resize lazily if the compositor handed us a new size. */
    if (c->pending_w != c->width || c->pending_h != c->height) {
        alloc_buffer(c, c->pending_w, c->pending_h);
    }
    return (fb_t){
        .px = c->pixels, .w = c->width, .h = c->height, .stride = c->stride_px,
    };
}

void wl_commit(wl_ctx_t *c) {
    if (!c->buffer) return;
    struct wl_callback *cb = wl_surface_frame(c->surface);
    wl_callback_add_listener(cb, &frame_listener, c);
    wl_surface_attach(c->surface, c->buffer, 0, 0);
    wl_surface_damage_buffer(c->surface, 0, 0, c->width, c->height);
    wl_surface_commit(c->surface);
    wl_display_flush(c->display);
    c->frame_ready = false;
}

bool wl_should_close(const wl_ctx_t *c) { return c->should_close; }
int  wl_fd(const wl_ctx_t *c)           { return wl_display_get_fd(c->display); }
bool wl_frame_ready(const wl_ctx_t *c)  { return c->frame_ready; }

bool wl_pump(wl_ctx_t *c) {
    wl_display_flush(c->display);
    int r = wl_display_dispatch_pending(c->display);
    if (r > 0) return true;
    /* Read whatever the compositor sent. */
    while (wl_display_prepare_read(c->display) != 0) {
        if (wl_display_dispatch_pending(c->display) < 0) return false;
    }
    wl_display_read_events(c->display);
    return wl_display_dispatch_pending(c->display) > 0;
}

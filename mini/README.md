# Departure HUD — `mini`

A direct-to-Wayland C rewrite of the HUD. Same panels, same retro look, but
no Qt, no QML engine, no Mesa/LLVM, and no NVIDIA libraries at runtime. It
draws everything itself into a single shared-memory buffer with a hand-rolled
software rasteriser.

| | Qt build | `mini` |
| --- | --- | --- |
| RSS         | ~298 MB | **~17 MB** |
| PSS         | ~105 MB | **~6 MB** |
| CPU (idle)  | ~9 %    | **~1.5 %** |
| threads     | 18      | **3** |
| binary      | 1.5 MB  | **~240 KB** |
| runtime deps | qt6, layer-shell-qt, mesa, libnvidia-ml | wayland-client, freetype, pixman, alsa-lib |

All the panels from the Qt build are here: the SYS TIME / SYSMON / UPTIME
header; CPU (per-core load + frequency); MEM (used/cache/swap); AUDIO with
the semicircular VolumeKnob (incl. the OVERLOAD state); GPU; THERM
(CPU/GPU/SSD/chassis); BATT with DRAIN %/H; DISK; NET; DISPLAY; the centre
sphere scope with its animated star field; the PitchStack and YearStack; and
the footer with host/kernel/shell/user and the load average.

---

## 1. Dependencies

Runtime + build use the same shared libraries; you also need
`wayland-scanner` (ships with `wayland`) and a C compiler.

| Library | Why |
| --- | --- |
| `wayland-client` | the Wayland protocol |
| `wayland-protocols` | `xdg-shell` XML (bundled copy also in `protocols/`) |
| `freetype2` | glyph rasterisation of the bundled Departure Mono font |
| `pixman` | pixel blits |
| `alsa-lib` | master-volume readout via `snd_mixer` (no PulseAudio/scripts) |

Install:

```sh
# Arch
sudo pacman -S --needed base-devel wayland wayland-protocols freetype2 pixman alsa-lib

# Debian/Ubuntu
sudo apt install build-essential libwayland-dev wayland-protocols \
                 libfreetype-dev libpixman-1-dev libasound2-dev

# Fedora
sudo dnf install gcc make wayland-devel wayland-protocols-devel \
                 freetype-devel pixman-devel alsa-lib-devel
```

You also need a Wayland compositor that implements **`wlr-layer-shell-v1`**:
sway, Hyprland, river, niri, Wayfire, KDE Plasma 6, … (GNOME/Mutter does
**not** implement it).

---

## 2. Build

```sh
cd mini
make            # → ./departure-hud-mini
```

Other targets:

| target | does |
| --- | --- |
| `make`      | build the binary |
| `make run`  | build, then launch it |
| `make clean`| remove objects, generated protocol code, and the binary |

`make` runs `wayland-scanner` on `protocols/*.xml` into `src/gen/` and then
compiles. No CMake, no codegen step you have to run by hand.

---

## 3. Run

```sh
./departure-hud-mini
```

The font is searched for in this order, so running from the repo root or
from `mini/` both work:

1. `./fonts/DepartureMono-Regular.otf`
2. `../fonts/DepartureMono-Regular.otf`  ← used when run from `mini/`
3. `/usr/share/departure-hud/fonts/DepartureMono-Regular.otf`  ← after install

To install system-wide, copy the binary into your `PATH` and the font into
`/usr/share/departure-hud/fonts/`.

---

## 4. Configuration

Everything is controlled by environment variables — there's no config file.

| variable | values | default |
| --- | --- | --- |
| `DEPARTURE_HUD_POSITION` | `fullscreen` · `center` · `top-left` · `top-right` · `bottom-left` · `bottom-right` | `fullscreen` |
| `DEPARTURE_HUD_LAYER`    | `bottom` · `background` · `top` · `overlay` | `bottom` |
| `DEPARTURE_HUD_OUTPUT`   | connector name, e.g. `DP-1`, `eDP-1`, `HDMI-A-1` | compositor picks |
| `DEPARTURE_HUD_SCALE`    | absolute scale factor, e.g. `1.0`, `2.5` | auto fit-to-screen |
| `DEPARTURE_HUD_BG`       | `#RRGGBB` (opaque) or `#AARRGGBB` (translucent) | `#0d0d0d` opaque |
| `DEPARTURE_HUD_ACCENT`   | `#RRGGBB` / `#AARRGGBB` — text, bars, frames | `#f08a28` |
| `DEPARTURE_HUD_HOT`      | `#RRGGBB` / `#AARRGGBB` — alert/over-limit colour | `#ff5a3c` |
| `DEPARTURE_HUD_DISKS`    | comma-separated mount paths | `/` |
| `DEPARTURE_HUD_NVML`     | `1` to load `libnvidia-ml.so` for NVIDIA GPU stats | unset |

### Placement — where it sits and on which screen

- **`DEPARTURE_HUD_LAYER`** picks the `wlr-layer-shell` layer.
  `bottom` (default) is a desktop widget: drawn above the wallpaper but
  **below** normal windows, so opening a window covers it. `overlay` pins it
  on top of everything (notifications/lock excluded by some compositors).
  `background`/`top` are the in-between layers.
- **`DEPARTURE_HUD_OUTPUT`** pins the surface to one monitor by its connector
  name (the same names `wlr-randr` or your compositor config use). If the
  name doesn't exist, the program prints the list of available connectors and
  lets the compositor decide. Left unset, the compositor places it (usually
  the focused/primary output).
- **`DEPARTURE_HUD_POSITION`** `fullscreen` (default) stretches across the
  whole output; the corner/`center` values make a native-size 1180×600
  surface placed in that spot.

### Size & aspect ratio

In `fullscreen`, the 1180×600 design surface is fit into the output with
`scale = min(W/1180, H/600)` — exactly like the Qt build. That **preserves
the proportions** and letterboxes the spare space, so it looks right on 16:9,
21:9 ultrawide, portrait, whatever. Fonts are re-rendered by FreeType at the
scaled pixel size, so text stays crisp (it is not bitmap-upscaled).

Force a specific size with `DEPARTURE_HUD_SCALE` (absolute: `1.0` = the native
1180×600, `2.0` = double, …). Handy on 4K where auto-fit may be larger than
you want.

### Appearance

- **Colours** take `#RRGGBB` or `#AARRGGBB`. `DEPARTURE_HUD_ACCENT` drives the
  text/bars/frames; the dim and soft shades are derived from it automatically.
- **Transparency** comes from the alpha byte of `DEPARTURE_HUD_BG`. A 6-digit
  value is fully opaque (the Qt default look). An 8-digit value blends the
  panel over your wallpaper — e.g. `#A00d0d0d` ≈ 63 % opaque, `#400d0d0d`
  almost see-through. Bars and text always stay opaque; only the panel fill
  blends.

### Data sources

- **GPU**:
  - **AMD** (`amdgpu`/`nouveau`) — load% (`gpu_busy_percent`) and clock
    (`pp_dpm_sclk`) from `/sys/class/drm`, no extra library.
  - **Intel** (`i915`/`xe`) — clock from `gt_*_freq_mhz`. Load% comes from
    the i915 perf PMU (the same counters `intel_gpu_top` uses); the busiest
    engine's busy-time is sampled each second. That counter is privileged,
    so grant the binary access once:
    ```sh
    sudo setcap cap_perfmon+ep ./departure-hud-mini     # kernel ≥ 5.9
    # older kernels: sudo setcap cap_sys_admin+ep ./departure-hud-mini
    # or system-wide: sudo sysctl kernel.perf_event_paranoid=1
    ```
    Without it the GPU panel just shows the clock (load stays 0).
  - **NVIDIA** — set `DEPARTURE_HUD_NVML=1` to `dlopen` `libnvidia-ml.so`
    for load/clock/temp (it pulls in ~6 MB of libcuda, hence opt-in).
- **Audio**: ALSA's `snd_mixer` on the `default` device, which maps to
  PipeWire/PulseAudio's ALSA plugin on modern setups. No scripts, no daemon
  client.
- **Disks**: `DEPARTURE_HUD_DISKS=/,/home,/data` chooses the mounts shown.

### Examples

```sh
# Translucent desktop widget on the laptop panel
DEPARTURE_HUD_BG='#B00d0d0d' DEPARTURE_HUD_OUTPUT=eDP-1 ./departure-hud-mini

# Always-on-top green overlay on the external monitor, fixed 1.5× size
DEPARTURE_HUD_LAYER=overlay DEPARTURE_HUD_ACCENT='#39ff14' \
DEPARTURE_HUD_OUTPUT=DP-1 DEPARTURE_HUD_SCALE=1.5 ./departure-hud-mini

# Show extra mounts and enable NVIDIA stats
DEPARTURE_HUD_DISKS=/,/home,/mnt/data DEPARTURE_HUD_NVML=1 ./departure-hud-mini
```

### Autostart

Add to your compositor config, e.g. sway/Hyprland:

```
exec /path/to/departure-hud-mini    # sway
exec-once = /path/to/departure-hud-mini   # Hyprland
```

---

## 5. Not implemented yet

- **Multi-monitor at once** — one instance draws on one output. Launch a
  second instance with a different `DEPARTURE_HUD_OUTPUT` for a second screen.
- **Config file** — settings are env vars only (no `config.json`).
- **Intel GPU load %** — only the clock is available from sysfs; load would
  need perf events.
- **Audio output type** — the SP/HP/BT/HD strip is decorative.

---

## 6. Source layout

```
mini/
  Makefile            build (+ wayland-scanner codegen)
  protocols/          vendored xdg-shell + wlr-layer-shell XML
  src/
    main.c            layout, scaling, the render loop
    wl.c / wl.h       Wayland: registry, layer-shell, shm buffer, outputs
    draw.c / draw.h   software rasteriser (rects, lines, circles, arcs, glyph blit)
    font.c / font.h   FreeType glyph cache, monospace text
    sys.c / sys.h     /proc + /sys readers, sysfs/NVML GPU
    audio.c / audio.h ALSA master-volume readout
```

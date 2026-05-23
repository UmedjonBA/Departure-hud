# Departure HUD — `mini`

Direct-to-Wayland C rewrite of the main HUD. Same panels, same look, no Qt:

| | Qt version | mini |
| --- | --- | --- |
| RSS         | ~298 MB | **~12 MB** |
| PSS         | ~105 MB | **~3.6 MB** |
| CPU (idle)  | ~9 %    | **~2.5 %** |
| threads     | 18      | **3** |
| binary      | 1.5 MB  | **~240 KB** |
| deps        | qt6, layer-shell-qt, mesa, libnvidia-ml | wayland-client, freetype, pixman, alsa |

Same panels: SYS TIME / SYSMON / UPTIME, CPU (load + freq), MEM, AUDIO
(VolumeKnob with OVERLOAD), GPU (sysfs amdgpu/i915/xe; NVML opt-in),
THERM, BATT (with DRAIN %/H), DISK, NET, DISPLAY, identity & load avg in
the footer, sphere scope with animated stars, PitchStack, YearStack.

## Build

```sh
make
```

Dependencies (Arch package names): `wayland-protocols`, `wayland`,
`freetype2`, `pixman`, `alsa-lib`. Build uses `wayland-scanner` to
generate the protocol bindings.

## Run

```sh
./departure-hud-mini
```

Needs a Wayland compositor that implements `wlr-layer-shell-v1`
(sway, Hyprland, river, KDE Plasma 6, niri, ...).

## Configuration (env vars)

| variable | values | default |
| --- | --- | --- |
| `DEPARTURE_HUD_POSITION` | `fullscreen`, `center`, `top-left`, `top-right`, `bottom-left`, `bottom-right` | `fullscreen` |
| `DEPARTURE_HUD_LAYER`    | `bottom`, `background`, `top`, `overlay` | `bottom` |
| `DEPARTURE_HUD_DISKS`    | comma-separated mount paths, e.g. `/,/home` | `/` |
| `DEPARTURE_HUD_ACCENT`   | `#RRGGBB` or `#AARRGGBB` | `#f08a28` |
| `DEPARTURE_HUD_HOT`      | hex color | `#ff5a3c` |
| `DEPARTURE_HUD_BG`       | `#RRGGBB` (opaque panel) or `#AARRGGBB` (translucent) | `#0d0d0d` opaque |
| `DEPARTURE_HUD_OUTPUT`   | connector name to pin to, e.g. `DP-1`, `eDP-1` | compositor picks |
| `DEPARTURE_HUD_SCALE`    | absolute scale factor (overrides fit-to-screen) | auto-fit |
| `DEPARTURE_HUD_NVML`     | `1` to load `libnvidia-ml.so` for NVIDIA GPU stats | unset |

Transparency: pass an 8-digit `#AARRGGBB` to `DEPARTURE_HUD_BG` — the first
byte is alpha (`#000000` = fully opaque if you give 6 digits; `#A00d0d0d` ≈
63% opaque). Bars and text always stay opaque; only the panel fill blends.

Aspect ratio: the 1180×600 design is fit into the output with
`min(W/1180, H/600)` so it keeps its proportions and letterboxes on any
ratio (16:9, 21:9, portrait …). `DEPARTURE_HUD_SCALE` lets you force a size
instead; an unknown `DEPARTURE_HUD_OUTPUT` prints the list of connector
names the compositor exposes.

`fullscreen` (default) scales the 1180×600 design up to fill the output —
fonts are re-rendered at the scaled size so text stays crisp. Corner/center
positions use a native-size 1180×600 window. The default `bottom` layer makes
the HUD a desktop widget (above the wallpaper, below windows); use
`DEPARTURE_HUD_LAYER=overlay` to pin it on top.

The font is loaded from `./fonts/`, `../fonts/`, or
`/usr/share/departure-hud/fonts/`, in that order.

## What's not here yet

- Multi-monitor (Qt version spawns one HUD per output).
- JSON config file (`mini` uses env vars only).
- GPU load% for Intel i915 (would need perf events; we show clock only).
- Audio output type detection (the SP/HP/BT/HD strip is decorative).

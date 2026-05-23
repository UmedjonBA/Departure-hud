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
| `DEPARTURE_HUD_POSITION` | `center`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `fullscreen` | `center` |
| `DEPARTURE_HUD_LAYER`    | `overlay`, `top`, `bottom`, `background` | `overlay` |
| `DEPARTURE_HUD_DISKS`    | comma-separated mount paths, e.g. `/,/home` | `/` |
| `DEPARTURE_HUD_ACCENT`   | `#RRGGBB` or `#AARRGGBB` | `#f08a28` |
| `DEPARTURE_HUD_HOT`      | hex color | `#ff5a3c` |
| `DEPARTURE_HUD_BG`       | hex color (panel; alpha forced to `E0`) | `#0d0d0d` |
| `DEPARTURE_HUD_NVML`     | `1` to load `libnvidia-ml.so` for NVIDIA GPU stats | unset |

The font is loaded from `./fonts/`, `../fonts/`, or
`/usr/share/departure-hud/fonts/`, in that order.

## What's not here yet

- Multi-monitor (Qt version spawns one HUD per output).
- JSON config file (`mini` uses env vars only).
- GPU load% for Intel i915 (would need perf events; we show clock only).
- Audio output type detection (the SP/HP/BT/HD strip is decorative).

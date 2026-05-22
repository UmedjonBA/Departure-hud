# Departure HUD — standalone Wayland app

A retro-terminal SysMon HUD in the Departure Mono pixel font: per-core CPU,
memory, thermals, battery, disks, network, brightness, audio, and a
wireframe-sphere scope. Renders as a centered surface on Wayland with input
passthrough — behaves like a desktop widget that sits above the wallpaper and
below your apps.

This is a self-contained port of the original
[`departure-hud` Noctalia plugin](https://github.com/noctalia-dev/noctalia-plugins) —
identical visuals, but now config-driven JSON and launched from a terminal,
no Noctalia required.

![preview](preview.png)

---

## Requirements

- A Wayland compositor that implements **`wlr-layer-shell`**: Hyprland, Sway,
  Niri, river, Wayfire, KWin (Plasma 6), labwc, …
  GNOME/Mutter does **not** support layer-shell — see *GNOME workaround* below.
- **`quickshell`** ≥ 0.1 — the QML runtime that drives the HUD
  ([github.com/quickshell-mirror/quickshell](https://github.com/quickshell-mirror/quickshell)).
  The binary is called `quickshell` (on Arch it's the `noctalia-qs` fork,
  installed at `/usr/bin/quickshell`).
- **Qt 6** ≥ 6.6 with the QtWayland module (pulled in transitively by quickshell).
- Standard Linux: `/proc`, `/sys`, `df` (`coreutils`), plus optionally
  `wpctl` (PipeWire) or `pactl` (PulseAudio) for the volume readout.
- A monospace font with the basic ASCII set if you swap `Departure Mono` for
  something else. Departure Mono is bundled in `fonts/`.

---

## Installing quickshell per distro

Quickshell is the only non-trivial dependency. Pick your distro:

### Arch / Manjaro / EndeavourOS
```sh
pacman -S quickshell                # main repo
# or, AUR:
yay -S quickshell-git
```

### Gentoo
There's a maintained ebuild in the [`guru`](https://wiki.gentoo.org/wiki/Project:GURU) overlay:
```sh
eselect repository enable guru
emaint sync -r guru
emerge --ask gui-apps/quickshell
```
If `guru` doesn't have it yet, build from source — instructions below.

### Debian / Ubuntu
No official package as of writing. Build from source:
```sh
sudo apt install build-essential cmake ninja-build pkg-config git \
    qt6-base-dev qt6-declarative-dev qt6-wayland-dev qt6-svg-dev \
    qt6-shadertools-dev libwayland-dev libpipewire-0.3-dev \
    libdrm-dev libjemalloc-dev libxkbcommon-dev libcli11-dev \
    spirv-tools
git clone https://github.com/quickshell-mirror/quickshell
cd quickshell
cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### Fedora
```sh
sudo dnf copr enable errornointernet/quickshell
sudo dnf install quickshell
```
Or build from source the same way (Qt6 dev packages are `qt6-qtbase-devel`,
`qt6-qtdeclarative-devel`, `qt6-qtwayland-devel`).

### NixOS / Nix
The `quickshell` package is in nixpkgs:
```nix
environment.systemPackages = [ pkgs.quickshell ];
```
Or `nix-shell -p quickshell`.

### Anything else: build from source
Same recipe as the Debian section — Qt 6 dev packages, CMake, Ninja,
PipeWire, libwayland, libxkbcommon, libdrm, libjemalloc, CLI11, spirv-tools.

---

## Installing the HUD

```sh
git clone <this-repo> ~/dev/departure-hud      # or copy the directory
cd ~/dev/departure-hud
./departure-hud --install-config               # writes ~/.config/departure-hud/config.json
./departure-hud                                # run it
```

Put it on `$PATH`:
```sh
ln -s ~/dev/departure-hud/departure-hud ~/.local/bin/departure-hud
```

To autostart with your compositor, see *Autostart* below.

---

## Running

```sh
departure-hud                         # use $XDG_CONFIG_HOME/departure-hud/config.json
departure-hud -c /path/to/config.json # explicit config
departure-hud --install-config        # copy the sample config (won't overwrite)
departure-hud --print-config          # print the active config path and exit
```

The launcher does `exec quickshell -p shell.qml`, so it holds the terminal.

**Close it:**
- `Ctrl+C` in the same terminal, or
- from anywhere: `quickshell kill -p ~/dev/departure-hud/shell.qml`

**Run in the background** (release the terminal):
```sh
quickshell -d -p ~/dev/departure-hud/shell.qml
```

---

## Configuration

Settings live in a JSON file. The first that exists wins:

1. `$DEPARTURE_HUD_CONFIG` (env var or `-c`/`--config`)
2. `$XDG_CONFIG_HOME/departure-hud/config.json` (i.e. `~/.config/departure-hud/config.json`)
3. The bundled `config.json` next to the script

You only need to set the keys you want to change — anything missing falls
back to the built-in default. Comments are not valid JSON; the snippet below
uses `//` only for documentation.

```jsonc
{
  // ── Appearance ───────────────────────────────────────────────
  "scale":         1.0,         // multiplier for the base 1180×600 layout
                                // 0.5 = half-size, 2.0 = double, etc.

  "useBackground": false,       // false = fully transparent backdrop, wallpaper/apps show through
                                // true  = paint a solid rectangle in `bgColor` behind the HUD

  "accentColor":   "#f08a28",   // main UI color (text, bars, frames). Hex #rrggbb.
  "hotColor":      "#ff5a3c",   // alert color used for thresholds (over-temp, low battery)
  "bgColor":       "#0d0d0d",   // background fill — only visible if useBackground = true

  // ── Data sources ─────────────────────────────────────────────
  "updateMs":      1000,        // how often to re-poll /proc and /sys (ms)
                                // 200 = very smooth, 2000 = relaxed

  "disksToShow":   "/,/home",   // comma-separated mount points for the DISK panel
                                // e.g. "/,/home,/mnt/data"

  "netInterface":  "",          // pin a specific NIC ("eth0", "wlp3s0", "wg0"…)
                                // empty = use netMode below
  "netMode":       "sum",       // "sum"  = add all non-loopback interfaces
                                // "auto" = pick the busiest interface right now

  // ── Alert thresholds (°C) — values above 95% of these glow `hotColor` ──
  "cpuMaxTemp":    90,
  "gpuMaxTemp":    85,
  "ssdMaxTemp":    65,

  // ── Center scope ─────────────────────────────────────────────
  "showScope":     true,        // false = hide the wireframe-sphere panel
  "starCount":     30,          // warp-speed star count, 0 disables the effect

  // ── Optional GPU info script ─────────────────────────────────
  // Path to a script printing JSON like:
  //   {"text":"50°C","tooltip":"Temperature: 50°C\nUtilization: 39%\n
  //                             Power Usage: 6.24/[N/A] W\nClock Speed: 375/2100 MHz"}
  // If absent or non-executable, the GPU panel just hides.
  "gpuScriptPath": "~/.local/bin/gpuinfo.sh",

  // ── Wayland surface placement ────────────────────────────────
  "screen":        "",          // "" = primary; or an output name like "DP-1" / "eDP-1" / "HDMI-A-1"
                                // Run `wlr-randr` / `hyprctl monitors` to see your outputs.

  "layer":         "bottom",    // "background" | "bottom" | "top" | "overlay"
                                //   background = under wallpaper-handling layers
                                //   bottom     = above wallpaper, below normal windows  (default — like a desktop widget)
                                //   top        = above normal windows, below overlays
                                //   overlay    = above everything (notifications, lock screens)

  "clickThrough":  true,        // true  = empty area around the HUD passes clicks to apps below
                                // false = the whole layer surface intercepts input
                                //         (useful with keyboardFocus = ondemand)

  "keyboardFocus": "none"       // "none"      = never receives keyboard input
                                // "ondemand"  = receives focus when clicked
                                // "exclusive" = grabs keyboard focus (don't use unless you know why)
}
```

### Customizing the font

The HUD bundles Departure Mono (`fonts/DepartureMono-Regular.otf`) and loads
it at runtime, so no system install is needed. The font is referenced in
`Hud.qml` at:

```qml
FontLoader {
  id: depFont
  source: Qt.resolvedUrl("fonts/DepartureMono-Regular.otf")
}
readonly property string fontFamily: depFont.name || "monospace"
```

To use a different font, either:

1. **Replace the file** — drop your `.otf`/`.ttf` into `fonts/` and update the
   `source:` path in `Hud.qml`. The visual layout is hand-tuned to Departure
   Mono's metrics; another monospace will work but column widths may shift.
2. **Use a system font** — change the `FontLoader` to a name:
   ```qml
   readonly property string fontFamily: "JetBrains Mono"
   ```
   and delete the `FontLoader` block.

A `fontFamily` / `fontPath` setting in `config.json` is not implemented yet —
ask if you'd like it added.

### Examples

**Fully transparent, scaled up 1.4×, all alerts on a cold AMD box:**
```json
{
  "scale": 1.4,
  "useBackground": false,
  "accentColor": "#86c8d4",
  "hotColor":    "#e06c75",
  "cpuMaxTemp": 75,
  "gpuMaxTemp": 70,
  "showScope": false
}
```

**Pinned to a second monitor, opaque, always on top:**
```json
{
  "screen": "HDMI-A-1",
  "useBackground": true,
  "bgColor": "#000000",
  "layer": "overlay",
  "clickThrough": false,
  "keyboardFocus": "ondemand"
}
```

**Quiet mode (slower polling, single disk):**
```json
{
  "updateMs": 2500,
  "disksToShow": "/",
  "starCount": 0,
  "netMode": "auto"
}
```

---

## Autostart

### Hyprland
```conf
# ~/.config/hypr/hyprland.conf
exec-once = quickshell -d -p ~/dev/departure-hud/shell.qml
```

### Sway
```conf
# ~/.config/sway/config
exec quickshell -d -p ~/dev/departure-hud/shell.qml
```

### Niri
```kdl
# ~/.config/niri/config.kdl
spawn-at-startup "quickshell" "-d" "-p" "/home/USER/dev/departure-hud/shell.qml"
```

### Generic — XDG autostart
Create `~/.config/autostart/departure-hud.desktop`:
```ini
[Desktop Entry]
Type=Application
Name=Departure HUD
Exec=/home/YOU/dev/departure-hud/departure-hud
X-GNOME-Autostart-enabled=true
OnlyShowIn=Hyprland;sway;niri;river;Wayfire;
```

### systemd user service
`~/.config/systemd/user/departure-hud.service`:
```ini
[Unit]
Description=Departure HUD
PartOf=graphical-session.target
After=graphical-session.target

[Service]
ExecStart=%h/dev/departure-hud/departure-hud
Restart=on-failure

[Install]
WantedBy=graphical-session.target
```
Then:
```sh
systemctl --user daemon-reload
systemctl --user enable --now departure-hud
```

---

## GNOME workaround

GNOME/Mutter does not implement `wlr-layer-shell`, so the HUD cannot anchor
itself the normal way. Two options:

1. **Use a wlroots-based compositor** for the session (Sway, Hyprland, …).
2. **Run inside an Xwayland override-redirect window** — beyond the scope of
   this app; would require changes to `shell.qml`. Open an issue if you need
   this.

KDE Plasma 6 supports layer-shell natively and works out of the box.

---

## Troubleshooting

**Nothing appears, no errors.**
Check the actual surface was created on your screen — run with verbose
logging:
```sh
quickshell -v -p ~/dev/departure-hud/shell.qml
```
On compositors with strict output protection you may need to set `"screen"`
explicitly (use `wlr-randr` or `hyprctl monitors` to find the name).

**The HUD opens on top of all windows.**
You're still on the old default. Set `"layer": "bottom"` in your config (or
delete the field to inherit the current default).

**Battery / brightness / GPU panels are missing.**
By design — they hide when the hardware isn't there. For GPU, you need a
script at `gpuScriptPath` that prints JSON in the documented format.

**Wrong network interface picked.**
Set `"netInterface": "wlp3s0"` (or whatever your NIC is) to pin it. Or use
`"netMode": "auto"` to track the busiest one over time.

**"noctalia-qs was built against Qt 6.11.0 but the system has updated to
6.11.1" warning.**
Harmless cosmetic warning from the Arch fork — rebuild the `noctalia-qs`
package against current Qt if it bothers you.

**Layer-shell errors / missing protocol.**
Your compositor does not implement `wlr-layer-shell`. See *GNOME workaround*.

---

## Files

| File             | Purpose                                                  |
| ---------------- | -------------------------------------------------------- |
| `shell.qml`      | Entry point — loads JSON config, opens the Wayland surface |
| `SysData.qml`    | System data collector (CPU, mem, batt, …)                |
| `Hud.qml`        | Pure presentation — binds to `SysData`                   |
| `config.json`    | Sample / fallback config                                 |
| `departure-hud`  | Bash launcher                                            |
| `fonts/`         | Bundled Departure Mono                                   |
| `preview.png`    | Screenshot used in this README                           |

---

## License

- HUD code: MIT.
- Departure Mono: © Helena Zhang — see `fonts/LICENSE.txt` and `fonts/NOTICE.md`.

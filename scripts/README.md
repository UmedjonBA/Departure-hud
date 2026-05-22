# GPU info scripts

The Departure HUD's GPU panel is filled by an **external script** that prints
one line of JSON. This keeps the binary vendor-agnostic.

Pick the script that matches your GPU, drop it in `$PATH` (or anywhere you
like), and point `gpuScriptPath` in your config at it.

## Pick one

| Script                  | Best for                              | Extra packages                |
| ----------------------- | ------------------------------------- | ----------------------------- |
| `gpuinfo-nvidia.sh`     | NVIDIA proprietary driver             | `nvidia-utils` (`nvidia-smi`) |
| `gpuinfo-amd.sh`        | Modern AMD / Radeon                   | `amdgpu_top`, `jq`            |
| `gpuinfo-intel.sh`      | Intel integrated / Arc                | `intel-gpu-tools`, `jq` *(setcap required)* |
| `gpuinfo-sensors.sh`    | Any GPU exposing temp via `lm_sensors`| `lm_sensors`                  |

## Install

```sh
# pick one
cp scripts/gpuinfo-nvidia.sh   ~/.local/bin/gpuinfo.sh
# or:
cp scripts/gpuinfo-amd.sh      ~/.local/bin/gpuinfo.sh
# or:
cp scripts/gpuinfo-intel.sh    ~/.local/bin/gpuinfo.sh
# or:
cp scripts/gpuinfo-sensors.sh  ~/.local/bin/gpuinfo.sh

chmod +x ~/.local/bin/gpuinfo.sh
```

Then in `~/.config/departure-hud/config.json`:

```json
{ "gpuScriptPath": "~/.local/bin/gpuinfo.sh" }
```

Verify by running the script yourself first:

```sh
~/.local/bin/gpuinfo.sh
# expected: a single line of JSON like
# {"text":"50°C","tooltip":"Temperature: 50°C\nUtilization: 39%\n..."}
```

## JSON contract

The HUD parses the `tooltip` field with regexes:

- `Temperature: <N> °C`         → THERM panel + GPU panel header
- `Utilization: <N> %`          → GPU load bar
- `Power Usage: <N>/<M> W`      → power draw
- `Clock Speed: <N>/<M> MHz`    → clock bar with max as scale

If `tooltip` has no temperature, `text` is parsed as a fallback (the
first `<N>°C` in it).

Missing fields just hide their row — you don't need to provide all of them.
Empty output, non-zero exit, or invalid JSON all hide the GPU panel
silently; no errors are raised.

## Writing your own

The simplest possible script:

```sh
#!/usr/bin/env bash
echo '{"text":"50°C","tooltip":"Temperature: 50°C\nUtilization: 39%\nPower Usage: 6.24/[N/A] W\nClock Speed: 375/2100 MHz"}'
```

Replace the hardcoded values with whatever your tooling spits out. The HUD
re-runs the script every `updateMs` (default 1 s), so keep it fast.

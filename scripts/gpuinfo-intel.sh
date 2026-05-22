#!/usr/bin/env bash
# gpuinfo-intel.sh — drop-in script for Departure HUD's GPU panel.
#
# Requires: intel_gpu_top (from `intel-gpu-tools` / `igt-gpu-tools`) and jq.
#   pacman -S intel-gpu-tools jq           # Arch
#   apt    install intel-gpu-tools jq      # Debian/Ubuntu
#
# Notes:
#   - intel_gpu_top needs CAP_PERFMON (or root). Either run the HUD as root
#     (not recommended) or grant the capability:
#         sudo setcap cap_perfmon,cap_sys_admin+ep /usr/bin/intel_gpu_top
#   - Integrated Intel GPUs usually don't expose discrete power/clock
#     telemetry, so those rows will be omitted and the HUD hides them.
#
# Install:
#   cp scripts/gpuinfo-intel.sh ~/.local/bin/gpuinfo.sh
#   chmod +x ~/.local/bin/gpuinfo.sh
# Then set in ~/.config/departure-hud/config.json:
#   "gpuScriptPath": "~/.local/bin/gpuinfo.sh"

set -eu

if ! command -v intel_gpu_top >/dev/null 2>&1 || ! command -v jq >/dev/null 2>&1; then
  exit 0
fi

# Grab one 500 ms sample, then take the first JSON object out of the stream.
SAMPLE=$(timeout 1.2s intel_gpu_top -J -s 500 2>/dev/null \
         | jq -cs '.[0] // empty' 2>/dev/null || true)
[ -z "$SAMPLE" ] && exit 0

UTIL=$(jq -r '(.engines."Render/3D/0".busy // .engines["Render/3D"].busy // 0) | floor' <<<"$SAMPLE")
FREQ=$(jq -r '(.frequency.actual // 0) | floor' <<<"$SAMPLE")
FMAX=$(jq -r '(.frequency.requested // .frequency.actual // 0) | floor' <<<"$SAMPLE")

# Temperature comes from hwmon; the HUD reads that itself, so we just leave
# the text empty here — it will pull the temp from sysfs and show it on
# the THERM panel.  The GPU panel will simply show LOAD% and CLOCK.
printf '{"text":"INTEL","tooltip":"Utilization: %s%%\\nClock Speed: %s/%s MHz"}\n' \
  "$UTIL" "$FREQ" "$FMAX"

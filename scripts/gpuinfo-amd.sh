#!/usr/bin/env bash
# gpuinfo-amd.sh — drop-in script for Departure HUD's GPU panel.
#
# Requires: amdgpu_top (https://github.com/Umio-Yasuno/amdgpu_top) and jq.
#   pacman -S amdgpu_top jq        # Arch
#   apt    install amdgpu_top jq   # Debian/Ubuntu (where packaged)
#
# Install:
#   cp scripts/gpuinfo-amd.sh ~/.local/bin/gpuinfo.sh
#   chmod +x ~/.local/bin/gpuinfo.sh
# Then set in ~/.config/departure-hud/config.json:
#   "gpuScriptPath": "~/.local/bin/gpuinfo.sh"

set -eu

if ! command -v amdgpu_top >/dev/null 2>&1 || ! command -v jq >/dev/null 2>&1; then
  exit 0
fi

# Take a single 50 ms sample of the first AMD GPU on the system.
J=$(amdgpu_top -d -J -n 1 -s 50 2>/dev/null | jq '.devices[0] // empty')
if [ -z "$J" ]; then
  exit 0
fi

# Field paths vary slightly between amdgpu_top versions; use safe fallbacks.
TEMP=$(jq -r '
  (.Sensors."Edge Temperature".value
   // .Sensors."GPU Temperature".value
   // .Sensors."Junction Temperature".value
   // 0) | floor' <<<"$J")
UTIL=$(jq -r '(.gpu_activity.GFX.value // 0) | floor' <<<"$J")
POW=$( jq -r '(.Sensors."Average Power".value // .Sensors."GPU Power".value // 0) | tostring' <<<"$J")
CLK=$( jq -r '(.gpu_clock.value // .Sensors."GFX Clock".value // 0) | floor' <<<"$J")
CLK_MAX=$(jq -r '(.gpu_clock.max // 0) | floor' <<<"$J")

printf '{"text":"%s°C","tooltip":"Temperature: %s°C\\nUtilization: %s%%\\nPower Usage: %s/[N/A] W\\nClock Speed: %s/%s MHz"}\n' \
  "$TEMP" "$TEMP" "$UTIL" "$POW" "$CLK" "$CLK_MAX"

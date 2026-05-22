#!/usr/bin/env bash
# gpuinfo-sensors.sh — minimal "temperature only" GPU info script.
#
# Uses lm_sensors (pacman/apt/dnf install lm_sensors, then run `sensors-detect`).
# Works on any GPU that exposes a thermal sensor via hwmon (amdgpu, nouveau,
# i915, …). The other fields are omitted, so the HUD will hide them.
#
# Install:
#   cp scripts/gpuinfo-sensors.sh ~/.local/bin/gpuinfo.sh
#   chmod +x ~/.local/bin/gpuinfo.sh

set -eu

command -v sensors >/dev/null 2>&1 || exit 0

# Prefer the "edge" sensor (amdgpu), fall back to "GPU Temperature", then
# whatever the first nvme/edge/temp reads. Strip + and °C, keep the int.
TEMP=$(sensors 2>/dev/null | awk '
  /^(edge|GPU [Tt]emperature|junction):/ {
    gsub(/[+°C]/, "", $2)
    printf "%d", $2
    exit
  }')

[ -z "$TEMP" ] && exit 0

printf '{"text":"%s°C","tooltip":"Temperature: %s°C\\n"}\n' "$TEMP" "$TEMP"

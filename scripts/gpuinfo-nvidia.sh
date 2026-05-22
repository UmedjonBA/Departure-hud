#!/usr/bin/env bash
# gpuinfo-nvidia.sh — drop-in script for Departure HUD's GPU panel.
#
# Requires: nvidia-smi (ships with the NVIDIA proprietary driver).
#
# Install:
#   cp scripts/gpuinfo-nvidia.sh ~/.local/bin/gpuinfo.sh
#   chmod +x ~/.local/bin/gpuinfo.sh
# Then set in ~/.config/departure-hud/config.json:
#   "gpuScriptPath": "~/.local/bin/gpuinfo.sh"

set -eu

if ! command -v nvidia-smi >/dev/null 2>&1; then
  exit 0
fi

read -r TEMP UTIL POW POW_MAX CLK CLK_MAX < <(
  nvidia-smi \
    --query-gpu=temperature.gpu,utilization.gpu,power.draw,power.max_limit,clocks.gr,clocks.max.gr \
    --format=csv,noheader,nounits 2>/dev/null \
  | head -n1 | tr ',' ' '
)

# Fall back to "[N/A]" for fields nvidia-smi may report as "[Not Supported]".
sane() { case "$1" in ""|*"Not Supported"*|*"[N/A]"*) printf '[N/A]';; *) printf '%s' "$1";; esac; }
TEMP=$(sane "$TEMP")
UTIL=$(sane "$UTIL")
POW=$(sane "$POW")
POW_MAX=$(sane "$POW_MAX")
CLK=$(sane "$CLK")
CLK_MAX=$(sane "$CLK_MAX")

printf '{"text":"%s°C","tooltip":"Temperature: %s°C\\nUtilization: %s%%\\nPower Usage: %s/%s W\\nClock Speed: %s/%s MHz"}\n' \
  "$TEMP" "$TEMP" "$UTIL" "$POW" "$POW_MAX" "$CLK" "$CLK_MAX"

#!/usr/bin/env bash
# Build the Waveshare board firmware, merge to a single flashable image, and
# publish the web flasher to https://nullphase.net/oc/flash/ PLUS the config page
# to https://nullphase.net/oc/ (served as that directory's index -- it is
# ../config.html, self-contained: the QR encoder lib is vendored inline, so the
# one file is the whole deploy).
#
# The firmware is generic across all units -- per-unit tailoring is the Web
# Serial config (nullphase.net/oc/), not a reflash -- so this one image serves every
# unit. User flow: open the URL in Chrome/Edge, click Install, pick the port.
#
# Requires: the PlatformIO venv at ~/.platformio, and ssh/scp access to the server.
set -euo pipefail
cd "$(dirname "$0")"

PIO="$HOME/.platformio/penv/bin"
BUILD="../.pio/build/esp32-s3-touch-128"
BOOT0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
REMOTE="nullphase.net:nullphase/oc/flash/"

"$PIO/pio" run -d .. -e esp32-s3-touch-128

# One image at 0x0. Offsets are the Arduino-ESP32 defaults; --flash_mode/freq keep
# preserves the mode/freq bytes the build baked into bootloader.bin. 16MB flash.
"$PIO/python" "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" --chip esp32s3 merge_bin \
  -o ocellus.bin --flash_mode keep --flash_freq keep --flash_size 16MB \
  0x0     "$BUILD/bootloader.bin" \
  0x8000  "$BUILD/partitions.bin" \
  0xe000  "$BOOT0" \
  0x10000 "$BUILD/firmware.bin"

# Stamp the flasher version from git so the esp-web-tools dialog shows something
# real and traceable (build date + commit) instead of a hand-edited "1.0" that
# always lies. Repo manifest.json stays "dev"; only the deployed copy is stamped.
# -dirty flags a build made from uncommitted changes (won't reproduce from git).
VER="$(date +%Y.%m.%d)-$(git -C .. rev-parse --short HEAD)$(git -C .. diff --quiet HEAD 2>/dev/null || echo -dirty)"
MANIFEST="$(mktemp)"; trap 'rm -f "$MANIFEST"' EXIT
sed "s/\"version\": *\"[^\"]*\"/\"version\": \"$VER\"/" manifest.json > "$MANIFEST"

scp index.html jelly.js ocellus.bin "$REMOTE"
scp "$MANIFEST" "${REMOTE}manifest.json"                          # stamped, not the repo template
scp ../config.html "nullphase.net:nullphase/oc/index.html"        # config page = /oc/'s index
echo "Published $VER -> https://nullphase.net/oc/flash/ (+ config page at /oc/)"

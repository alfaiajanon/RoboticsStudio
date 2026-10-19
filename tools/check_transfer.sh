#!/bin/bash
# Transfer check: every firmware sketch of the demos and examples must also
# compile for its real board, unchanged. A board folder next to the sketch
# (demo/<name>/uno/robot_pins.h, examples/firmware/<name>/esp32cam/...) holds
# that board's robot_pins.h; the sketch and that header are copied into an
# Arduino sketch folder and compiled with arduino-cli.
#
#   tools/check_transfer.sh            all boards
#   tools/check_transfer.sh uno        only the Uno sketches
#
# Needs arduino-cli with the cores and libraries the sketches use:
#   uno:      arduino:avr, libraries Servo, "Adafruit ST7735 and ST7789 Library", "Adafruit GFX Library"
#   esp32cam: esp32:esp32, library ESP32Servo (board manager URL
#             https://espressif.github.io/arduino-esp32/package_esp32_index.json)
# ARDUINO_CLI overrides which arduino-cli is used.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
WORK="$ROOT/build/transfer"
ONLY="${1:-}"

command -v "$CLI" >/dev/null || { echo "arduino-cli not found (set ARDUINO_CLI)" >&2; exit 1; }

fqbn_for() {
    case "$1" in
        uno)      echo "arduino:avr:uno" ;;
        esp32cam) echo "esp32:esp32:esp32cam" ;;
        *)        echo "" ;;
    esac
}

checked=0
failed=0
shopt -s nullglob
for header in "$ROOT"/demo/*/*/robot_pins.h "$ROOT"/examples/firmware/*/*/robot_pins.h; do
    board="$(basename "$(dirname "$header")")"
    fqbn="$(fqbn_for "$board")"
    [ -n "$fqbn" ] || continue
    [ -z "$ONLY" ] || [ "$ONLY" = "$board" ] || continue
    project="$(dirname "$(dirname "$header")")"
    for ino in "$project"/*.ino; do
        # Arduino sketch folder = sketch name, letters/digits/underscores only.
        name="$(basename "$ino" .ino | tr -c 'A-Za-z0-9_\n' '_')"
        dir="$WORK/$board/$name"
        rm -rf "$dir" && mkdir -p "$dir"
        cp "$ino" "$dir/$name.ino"
        cp "$header" "$dir/robot_pins.h"

        echo "== ${ino#"$ROOT"/} for $fqbn"
        checked=$((checked + 1))
        if ! "$CLI" compile --fqbn "$fqbn" --warnings default "$dir"; then
            failed=$((failed + 1))
        fi
    done
done

echo "$((checked - failed))/$checked sketches compile for their real boards"
[ "$checked" -gt 0 ] && [ "$failed" -eq 0 ]

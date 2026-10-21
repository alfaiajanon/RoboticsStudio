#!/bin/bash
# Transfer check: every firmware sketch of the demos and examples must also
# compile for its real board, unchanged. Two kinds of sketch:
#   - Real projects (demo/<name>/<name>.rsproj with "level": "real"): each
#     board's firmware slot is compiled for that board's fqbn (from its
#     .rsdef), with the sketch's own files (its sketch folder's .h and .cpp).
#     These sketches use the board's own pins, as on the real board.
#   - Prototype sketches with a board folder next to them
#     (examples/firmware/<name>/uno/robot_pins.h, .../esp32cam/...): the
#     folder holds a hand-written robot_pins.h with the sketch's names on that
#     board's pins; the sketch and that header are compiled together.
# Each sketch is copied into an Arduino sketch folder and compiled with
# arduino-cli.
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

# compile <sketch.ino> <fqbn> <label> [headers...]
compile() {
    local ino="$1" fqbn="$2" label="$3"
    shift 3
    # Arduino sketch folder = sketch name, letters/digits/underscores only.
    local name dir
    name="$(basename "$ino" .ino | tr -c 'A-Za-z0-9_\n' '_')"
    dir="$WORK/$label/$name"
    rm -rf "$dir" && mkdir -p "$dir"
    cp "$ino" "$dir/$name.ino"
    for h in "$@"; do cp "$h" "$dir/"; done
    echo "== ${ino#"$ROOT"/} for $fqbn"
    checked=$((checked + 1))
    if ! "$CLI" compile --fqbn "$fqbn" --warnings default "$dir"; then
        failed=$((failed + 1))
    fi
}

shopt -s nullglob

# Real projects: "<sketch>\t<fqbn>\t<label>" per board with a firmware slot.
while IFS=$'\t' read -r ino fqbn label; do
    [ -n "$ino" ] || continue
    [ -z "$ONLY" ] || [ "$ONLY" = "$label" ] || continue
    # The sketch's own files: a sketch folder (script/<name>/) brings its
    # headers and .cpp files; a sketch at the project root only its headers.
    sketchdir="$(dirname "$ino")"
    if [ "$(basename "$sketchdir")" = "$(basename "$ino" .ino)" ]; then
        files=("$sketchdir"/*.h "$sketchdir"/*.hpp "$sketchdir"/*.cpp)
    else
        files=("$sketchdir"/*.h "$sketchdir"/*.hpp)
    fi
    compile "$ino" "$fqbn" "$label" "${files[@]}"
done < <(python3 - "$ROOT" <<'EOF'
import glob, json, os, sys
root = sys.argv[1]
def rsdef(model, project_dir):
    for base in (os.path.join(project_dir, "Components"), os.path.join(root, "models")):
        for path in glob.glob(os.path.join(base, "**", "*.rsdef"), recursive=True):
            try:
                d = json.load(open(path))
            except (OSError, ValueError):
                continue
            if d.get("id") == model:
                return d
    return None
for proj in sorted(glob.glob(os.path.join(root, "demo", "*", "*.rsproj"))):
    doc = json.load(open(proj))
    if doc.get("level") != "real":
        continue
    pdir = os.path.dirname(proj)
    for c in doc.get("assembly", {}).get("components", []):
        if not c.get("firmware"):
            continue
        d = rsdef(c.get("model_id"), pdir)
        fqbn = (d or {}).get("board", {}).get("fqbn", "")
        if fqbn:
            label = {"arduino:avr:uno": "uno", "esp32:esp32:esp32cam": "esp32cam"}.get(fqbn, fqbn.split(":")[-1])
            print(f"{os.path.join(pdir, c['firmware'])}\t{fqbn}\t{label}")
EOF
)

# Prototype sketches with a hand-written board header.
for header in "$ROOT"/demo/*/*/robot_pins.h "$ROOT"/examples/firmware/*/*/robot_pins.h; do
    board="$(basename "$(dirname "$header")")"
    fqbn="$(fqbn_for "$board")"
    [ -n "$fqbn" ] || continue
    [ -z "$ONLY" ] || [ "$ONLY" = "$board" ] || continue
    project="$(dirname "$(dirname "$header")")"
    for ino in "$project"/*.ino; do
        compile "$ino" "$fqbn" "$board" "$header"
    done
done

echo "$((checked - failed))/$checked sketches compile for their real boards"
[ "$checked" -gt 0 ] && [ "$failed" -eq 0 ]

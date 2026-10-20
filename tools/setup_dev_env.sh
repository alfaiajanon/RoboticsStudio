#!/bin/bash
# Sets up a fresh Ubuntu 24.04 machine (e.g. a cloud dev container) to build,
# run and check RoboticsStudio:
#   - system packages: Qt 6 + glfw + GL dev, Xvfb (headless runs)
#   - the pinned wasi-sdk (tools/fetch_wasi_sdk.sh): the firmware compiler
#   - arduino-cli with the AVR and ESP32 cores and the libraries that
#     tools/check_transfer.sh needs (skip with --no-arduino; several GB)
#   - a Debug build in build/ (skip with --no-build)
# Safe to run again: each step is skipped once done.
#
#   tools/setup_dev_env.sh [--no-arduino] [--no-build]
#
# Needs root or sudo for apt. Behind a proxy (HTTPS_PROXY), arduino-cli is
# pointed at it (it doesn't read the variable itself). Network access needed:
# the Ubuntu archive, github.com, downloads.arduino.cc, espressif.github.io.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARDUINO=1
BUILD=1
for arg in "$@"; do
    case "$arg" in
        --no-arduino) ARDUINO=0 ;;
        --no-build)   BUILD=0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

ARDUINO_CLI_VERSION=1.5.1
ARDUINO_DIR=/opt/arduino-cli
ESP32_INDEX=https://espressif.github.io/arduino-esp32/package_esp32_index.json
# Versions check_transfer.sh was last verified with.
AVR_CORE=arduino:avr@1.8.8
ESP32_CORE=esp32:esp32@3.3.12
LIBRARIES=("Servo@1.3.0" "ESP32Servo@3.2.1" "Adafruit ST7735 and ST7789 Library@1.11.0" "Adafruit GFX Library@1.12.6")

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"
step() { echo; echo "== $*"; }

# ---- system packages ----
PACKAGES=(build-essential cmake git curl python3 qt6-base-dev qt6-declarative-dev
          libglfw3-dev libgl-dev xvfb xauth)
missing=()
for p in "${PACKAGES[@]}"; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
if [ ${#missing[@]} -gt 0 ]; then
    step "apt: ${missing[*]}"
    $SUDO apt-get update -q
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -q "${missing[@]}"
else
    step "apt packages already installed"
fi

# ---- firmware compiler ----
step "wasi-sdk"
"$ROOT/tools/fetch_wasi_sdk.sh"

# ---- arduino-cli, cores and libraries (transfer check) ----
if [ "$ARDUINO" -eq 1 ]; then
    if ! command -v arduino-cli >/dev/null || ! arduino-cli version | grep -q "$ARDUINO_CLI_VERSION"; then
        step "arduino-cli $ARDUINO_CLI_VERSION"
        tmp="$(mktemp -d)"
        curl -fsSL -o "$tmp/cli.tgz" \
            "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz"
        $SUDO mkdir -p "$ARDUINO_DIR"
        $SUDO tar -xzf "$tmp/cli.tgz" -C "$ARDUINO_DIR" arduino-cli
        $SUDO ln -sf "$ARDUINO_DIR/arduino-cli" /usr/local/bin/arduino-cli
        rm -rf "$tmp"
    fi
    step "arduino-cli config, cores and libraries"
    [ -f "$HOME/.arduino15/arduino-cli.yaml" ] || arduino-cli config init >/dev/null
    arduino-cli config set board_manager.additional_urls "$ESP32_INDEX"
    proxy="${HTTPS_PROXY:-${https_proxy:-}}"
    if [ -n "$proxy" ]; then arduino-cli config set network.proxy "$proxy"; fi
    arduino-cli core update-index
    for core in "$AVR_CORE" "$ESP32_CORE"; do
        if arduino-cli core list | grep -q "^${core%@*} *${core#*@} "; then
            echo "$core already installed"
        else
            arduino-cli core install "$core"
        fi
    done
    arduino-cli lib install "${LIBRARIES[@]}"
fi

# ---- build ----
if [ "$BUILD" -eq 1 ]; then
    step "configure + build (Debug, build/)"
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "$ROOT/build" -j"$(nproc)"
fi

step "done"
echo "Run the app headless:   xvfb-run -a $ROOT/build/RoboticsStudio"
echo "Validate parts:         python3 tools/validate_rsdef.py"
[ "$ARDUINO" -eq 1 ] && echo "Transfer check:         tools/check_transfer.sh"
exit 0

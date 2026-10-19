#!/bin/bash
# Downloads the pinned wasi-sdk (clang + wasm-ld + wasi sysroot) into
# extern/wasi-sdk. The firmware compiler uses it to build robot sketches to
# WebAssembly. Re-running is a no-op once the pinned version is in place.
set -euo pipefail

VERSION=34.0
TAG=wasi-sdk-34
SHA256=b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4
ARCHIVE="wasi-sdk-${VERSION}-x86_64-linux.tar.gz"
URL="https://github.com/WebAssembly/wasi-sdk/releases/download/${TAG}/${ARCHIVE}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/extern/wasi-sdk"

if [ -f "$DEST/VERSION.pinned" ] && [ "$(cat "$DEST/VERSION.pinned")" = "$VERSION" ]; then
    echo "wasi-sdk $VERSION already in $DEST"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading $URL"
curl -fL --retry 3 -o "$TMP/$ARCHIVE" "$URL"
echo "$SHA256  $TMP/$ARCHIVE" | sha256sum -c -

rm -rf "$DEST"
mkdir -p "$DEST"
tar -xzf "$TMP/$ARCHIVE" -C "$DEST" --strip-components=1
echo "$VERSION" > "$DEST/VERSION.pinned"
echo "wasi-sdk $VERSION installed in $DEST"

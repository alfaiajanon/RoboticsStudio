#!/bin/bash
# Copies what the firmware compiler needs into an AppDir (or any prefix):
#   <prefix>/usr/lib/roboticsstudio/wasi-sdk    trimmed wasi-sdk (clang, wasm-ld, wasip1 sysroot)
#   <prefix>/usr/share/roboticsstudio/firmware  the Arduino core (resources/firmware)
# These are the folders FirmwareCompiler looks in next to the executable
# (<prefix>/usr/bin). Run tools/fetch_wasi_sdk.sh first.
#
#   tools/stage_firmware_toolchain.sh AppDir
#
# The trimmed toolchain keeps only what compiling a sketch uses (measured in
# the Phase 4 spike: 632 MB -> ~165 MB, ~35 MB compressed): the clang driver
# and wasm-ld, the LLVM/clang shared libraries, clang's builtin headers and
# wasip1 runtime, and the wasm32-wasip1 sysroot without the exceptions
# variant, LTO libraries and shared libc.
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <AppDir>" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/extern/wasi-sdk"
PREFIX="$(mkdir -p "$1" && cd "$1" && pwd)"
DEST="$PREFIX/usr/lib/roboticsstudio/wasi-sdk"
FW_DEST="$PREFIX/usr/share/roboticsstudio/firmware"

if [ ! -x "$SDK/bin/clang++" ]; then
    echo "wasi-sdk not found in $SDK; run tools/fetch_wasi_sdk.sh" >&2
    exit 1
fi

rm -rf "$DEST" "$FW_DEST"
mkdir -p "$DEST/bin" "$DEST/lib" "$DEST/share/wasi-sysroot/include" "$DEST/share/wasi-sysroot/lib"

# Driver and linker (clang, clang++ and wasm-ld are symlinks; -a keeps them).
CLANG_BIN="$(readlink "$SDK/bin/clang")"
cp -a "$SDK/bin/$CLANG_BIN" "$SDK/bin/clang" "$SDK/bin/clang++" "$SDK/bin/lld" "$SDK/bin/wasm-ld" "$DEST/bin/"
cp -a "$SDK"/bin/clang*.cfg "$DEST/bin/" 2>/dev/null || true
cp "$SDK/VERSION.pinned" "$DEST/"

# Shared libraries they load ($ORIGIN/../lib).
cp -a "$SDK"/lib/libLLVM.so* "$SDK"/lib/libclang-cpp.so* "$SDK"/lib/libedit.so* "$DEST/lib/"
# libedit needs libtinfo.so.6, which not every distribution ships under that name.
# (awk reads to the end, so ldconfig never gets SIGPIPE under pipefail.)
TINFO="$( (ldconfig -p 2>/dev/null || true) | awk '/libtinfo\.so\.6 .*x86-64/ && !found {print $NF; found = 1}')"
if [ -n "$TINFO" ]; then cp -L "$TINFO" "$DEST/lib/libtinfo.so.6"; fi

# clang's resource dir: builtin headers + the wasip1 runtime (compiler-rt).
RES="$(ls "$SDK/lib/clang")"
mkdir -p "$DEST/lib/clang/$RES/lib"
cp -a "$SDK/lib/clang/$RES/include" "$DEST/lib/clang/$RES/"
cp -a "$SDK/lib/clang/$RES/lib/wasm32-unknown-wasip1" "$DEST/lib/clang/$RES/lib/"

# Sysroot: wasm32-wasip1 only; static libs and crt objects, libc++ without exceptions.
SYS="$SDK/share/wasi-sysroot"
cp -a "$SYS/include/wasm32-wasip1" "$SYS/include/c++" "$DEST/share/wasi-sysroot/include/" 2>/dev/null \
    || cp -a "$SYS/include/wasm32-wasip1" "$DEST/share/wasi-sysroot/include/"
mkdir -p "$DEST/share/wasi-sysroot/lib/wasm32-wasip1"
( cd "$SYS/lib/wasm32-wasip1" && cp -a ./*.a ./*.o noeh "$DEST/share/wasi-sysroot/lib/wasm32-wasip1/" )

strip --strip-unneeded "$DEST/bin/$CLANG_BIN" "$DEST/bin/lld" "$DEST"/lib/*.so* 2>/dev/null || true

# The Arduino core.
mkdir -p "$(dirname "$FW_DEST")"
cp -a "$ROOT/resources/firmware" "$FW_DEST"

echo "Staged firmware toolchain: $(du -sh "$DEST" | cut -f1) in $DEST"
echo "Staged firmware core:      $(du -sh "$FW_DEST" | cut -f1) in $FW_DEST"

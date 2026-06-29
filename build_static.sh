#!/bin/sh
# Build headless, fully-static PDP-10 family simulators into ./sims/.
#
# The stock makefile/CMake build only links dynamically.  This script
# reuses the exact per-target compile command the makefile generates
# (via `make -n`) and re-links it as a fully-static, non-PIE (ET_EXEC)
# binary.  Such a binary depends only on the Linux syscall ABI, so it
# runs on any reasonably recent x86-64 Linux regardless of the host libc
# (built on Debian/glibc, it still runs on Alpine/musl and vice versa).
#
# The display devices (DPY/III/DD) only need SDL at *link* time.  At run
# time they default to an in-memory "shadow framebuffer" (see
# display/sim_ws.c) and never open a window, so we link against a minimal
# SDL2 built WITHOUT any graphics frontend (dummy video driver only).
# That keeps the static link self-contained: no X11, Wayland, ALSA,
# PulseAudio, libudev, ... dependencies to satisfy.
#
# For reproducibility the headless SDL2 is always built from source (the
# host's system SDL2 is whatever the distro shipped - often X11/Wayland
# enabled - so we do not rely on it).  Point SDL2_CONFIG at an existing
# headless static SDL2 to skip the source build.
#
# Prerequisites (both distros need a C toolchain + autotools + wget):
#   Debian/Ubuntu: apt-get install build-essential wget
#   Alpine:        apk add build-base linux-headers wget
#   (libsdl2-dev is NOT needed on either - we build our own headless SDL2)
#
# Optional knobs (environment):
#   SDL2_CONFIG=/path/to/sdl2-config   use an existing headless static SDL2
#   SDL_VERSION=2.30.9                 SDL2 release to fetch when building
#
# Usage: sh build_static.sh [target ...]
#        default targets: pdp6 pdp10-ka pdp10-ki pdp10-ks

set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
OUT=$ROOT/sims
DEPS=$ROOT/.static-deps                 # local prefix for a headless SDL2
SDL_VERSION=${SDL_VERSION:-2.30.9}

mkdir -p "$OUT"

TARGETS="${*:-pdp6 pdp10-ka pdp10-ki pdp10-ks}"

# --- 1. Build (once) a self-contained, headless static libSDL2.a ------------
#
# The same minimal SDL2 is built from source on every host so the result
# does not depend on whatever the distro's SDL2 package enabled.  An
# existing headless build can be reused via SDL2_CONFIG or $DEPS.
if [ -n "$SDL2_CONFIG" ] && [ -x "$SDL2_CONFIG" ]; then
    SDL_CONFIG=$SDL2_CONFIG
elif [ -x "$DEPS/bin/sdl2-config" ]; then
    SDL_CONFIG=$DEPS/bin/sdl2-config
else
    echo ">> Building headless static SDL2 $SDL_VERSION into $DEPS"
    mkdir -p "$DEPS/src"
    cd "$DEPS/src"
    tarball="SDL2-$SDL_VERSION.tar.gz"
    [ -f "$tarball" ] || wget -q "https://libsdl.org/release/$tarball"
    # Extract unless a complete tree (with configure) is already present.
    [ -f "SDL2-$SDL_VERSION/configure" ] || { rm -rf "SDL2-$SDL_VERSION"; tar xzf "$tarball"; }
    cd "SDL2-$SDL_VERSION"
    # Keep the full SDL API (so sim_video.c links) but drop every backend
    # that would introduce an external shared-library dependency.  The
    # dummy video/audio drivers remain and are all we need headless.
    ./configure --prefix="$DEPS" --disable-shared --enable-static \
        --without-x \
        --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm \
        --disable-video-vulkan \
        --disable-video-opengl --disable-video-opengles --disable-video-opengles2 \
        --disable-alsa --disable-pulseaudio --disable-jack --disable-pipewire \
        --disable-sndio --disable-esd --disable-arts --disable-nas \
        --disable-libudev --disable-dbus --disable-ibus --disable-fcitx \
        >/dev/null
    make -j"$(nproc 2>/dev/null || echo 2)" >/dev/null
    make install >/dev/null
    cd "$ROOT"
    SDL_CONFIG=$DEPS/bin/sdl2-config
fi
echo ">> Using SDL2: $SDL_CONFIG"

# --- 2. Build each target, re-linked fully static --------------------------
for t in $TARGETS; do
    echo "=== $t ==="
    rm -f "BIN/$t"
    # Grab the single gcc/cc invocation make would run for this target.
    cmd=$(make -n "$t" 2>/dev/null | grep -E '^(gcc|cc) .* -o ' | head -1)
    if [ -z "$cmd" ]; then
        echo "  !! could not obtain build command for $t" >&2
        exit 1
    fi
    # Drop optional-feature defines and their dynamic-only libraries (no
    # static .a for these is assumed, and the 1-bit BMP display dump needs
    # none of them).
    cmd=$(printf '%s' "$cmd" \
        | sed -e 's/-DHAVE_LIBPNG//g' -e 's/-DHAVE_ZLIB//g' -e 's/-DHAVE_EDITLINE//g' \
              -e 's/ -ledit / /g' -e 's/ -lpng / /g' -e 's/ -lz / /g')
    # Point SDL at our headless static build and link it statically.
    cmd=$(printf '%s' "$cmd" \
        | sed -e "s#\`sdl2-config --cflags\`#\`$SDL_CONFIG --cflags\`#g" \
              -e "s#\`sdl2-config --libs\`#\`$SDL_CONFIG --static-libs\`#g")
    # Force a fully-static, non-PIE (ET_EXEC) link; redirect output to sims/.
    cmd=$(printf '%s' "$cmd" | sed -e "s#-o BIN/$t#-static -no-pie -o $OUT/$t#")
    eval "$cmd"
    file "$OUT/$t"
done
echo "=== done: binaries in $OUT ==="

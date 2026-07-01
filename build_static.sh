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
#   GCC=musl-gcc or CC=musl-gcc        choose the compiler used for SDL/SIMH
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

if [ -n "$GCC" ]; then
    BUILD_CC=$GCC
elif [ -n "$CC" ]; then
    BUILD_CC=$CC
elif command -v musl-gcc >/dev/null 2>&1; then
    BUILD_CC=musl-gcc
elif command -v x86_64-linux-musl-gcc >/dev/null 2>&1; then
    BUILD_CC=x86_64-linux-musl-gcc
else
    BUILD_CC=gcc
fi

run_logged()
{
    desc=$1
    log=$2
    shift 2
    if ! "$@" >"$log" 2>&1; then
        echo "  !! $desc failed; log follows:" >&2
        sed -n '1,220p' "$log" >&2
        exit 1
    fi
}

filter_glibc_static_warnings()
{
    awk '
        /warning: Using '\''(dlopen|getaddrinfo|getservbyname)'\'' in statically linked applications requires at runtime the shared libraries from the glibc version used for linking/ {
            skip_prev = 0
            next
        }
        /\/usr\/bin\/ld: .*: note: the message above does not take linker garbage collection into account/ {
            next
        }
        /\/usr\/bin\/ld: .*: in function `[^'\'']*'\''[:]$/ {
            prev = $0
            skip_prev = 1
            next
        }
        {
            if (skip_prev) {
                print prev
                skip_prev = 0
            }
            print
        }
        END {
            if (skip_prev)
                print prev
        }
    ' "$1"
}

run_build()
{
    target=$1
    cmd=$2
    log=$OUT/$target.build.log
    filtered=$OUT/$target.build.filtered.log

    if ! sh -c "$cmd" >"$log" 2>&1; then
        cat "$log" >&2
        exit 1
    fi
    filter_glibc_static_warnings "$log" >"$filtered"
    if [ -s "$filtered" ]; then
        cat "$filtered" >&2
        exit 1
    fi
    rm -f "$log" "$filtered"
}

# --- 1. Build (once) a self-contained, headless static libSDL2.a ------------
#
# The same minimal SDL2 is built from source on every host so the result
# does not depend on whatever the distro's SDL2 package enabled.  An
# existing headless build can be reused via SDL2_CONFIG or $DEPS.
if [ -n "$SDL2_CONFIG" ] && [ -x "$SDL2_CONFIG" ]; then
    SDL_CONFIG=$SDL2_CONFIG
elif [ -x "$DEPS/bin/sdl2-config" ] && [ "$(cat "$DEPS/.compiler" 2>/dev/null)" = "$BUILD_CC" ]; then
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
    SDL_BUILD_LOG="$DEPS/src/SDL2-$SDL_VERSION.build.log"
    # Keep the full SDL API (so sim_video.c links) but drop every backend
    # that would introduce an external shared-library dependency.  The
    # dummy video/audio drivers remain and are all we need headless.
    run_logged "SDL2 configure" "$SDL_BUILD_LOG" \
        env CC="$BUILD_CC" ./configure --prefix="$DEPS" --disable-shared --enable-static \
        --without-x \
        --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm \
        --disable-video-vulkan \
        --disable-video-opengl --disable-video-opengles --disable-video-opengles2 \
        --disable-alsa --disable-pulseaudio --disable-jack --disable-pipewire \
        --disable-sndio --disable-esd --disable-arts --disable-nas \
        --disable-libudev --disable-dbus --disable-ibus --disable-fcitx
    run_logged "SDL2 build" "$SDL_BUILD_LOG" \
        make -j"$(nproc 2>/dev/null || echo 2)"
    run_logged "SDL2 install" "$SDL_BUILD_LOG" make install
    printf '%s\n' "$BUILD_CC" >"$DEPS/.compiler"
    cd "$ROOT"
    SDL_CONFIG=$DEPS/bin/sdl2-config
fi
echo ">> Using SDL2: $SDL_CONFIG"
echo ">> Using compiler: $BUILD_CC"

# --- 2. Build each target, re-linked fully static --------------------------
for t in $TARGETS; do
    echo "=== $t ==="
    rm -f "BIN/$t"
    # Grab the single gcc/cc invocation make would run for this target.
    cmd=$(make -n GCC="$BUILD_CC" "$t" 2>/dev/null | grep -E "^($BUILD_CC|gcc|cc) .* -o " | head -1)
    if [ -z "$cmd" ]; then
        echo "  !! could not obtain build command for $t" >&2
        exit 1
    fi
    # Drop optional-feature probes from the normal dynamic build.  These
    # either fail without host static archives (PCRE/VDE), add dynamic
    # loader calls (dlopen), or are not needed by these headless static
    # binaries.
    cmd=$(printf '%s' "$cmd" \
        | sed -e 's/-DHAVE_PCRE_H//g' \
              -e 's/-DSIM_HAVE_DLOPEN=[^ ]*//g' \
              -e 's/-DHAVE_VDE_NETWORK//g' \
              -e 's/-DHAVE_LIBPNG//g' \
              -e 's/-DHAVE_ZLIB//g' \
              -e 's/-DHAVE_EDITLINE//g' \
              -e 's/ -lpcre / /g' \
              -e 's/ -ldl / /g' \
              -e 's/ -lvdeplug / /g' \
              -e 's/ -ledit / /g' \
              -e 's/ -lpng / /g' \
              -e 's/ -lz / /g')
    # Point SDL at our headless static build and link it statically.
    cmd=$(printf '%s' "$cmd" \
        | sed -e "s#\`sdl2-config --cflags\`#\`$SDL_CONFIG --cflags\`#g" \
              -e "s#\`sdl2-config --libs\`#\`$SDL_CONFIG --static-libs\`#g")
    # Force a fully-static, non-PIE (ET_EXEC) link; discard unused socket
    # helpers so static glibc does not warn about unused NSS entry points.
    cmd=$(printf '%s' "$cmd" \
        | sed -e "s# -o BIN/$t# -ffunction-sections -fdata-sections -static -no-pie -Wl,--gc-sections -o $OUT/$t#")
    run_build "$t" "$cmd"
    file "$OUT/$t"
done
echo "=== done: binaries in $OUT ==="

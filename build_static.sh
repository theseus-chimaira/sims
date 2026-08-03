#!/bin/sh
# Build headless, fully-static PDP-10 family simulators into ./sims/.
#
# The stock makefile/CMake build only links dynamically.  This script
# reuses the exact per-target compile command the makefile generates
# (via `make -n`) and re-links it as a fully-static, non-PIE (ET_EXEC)
# binary.  Linux output depends only on the Linux syscall ABI, so it runs
# on any reasonably recent Linux system of the same architecture regardless
# of the host libc.  OpenBSD output similarly runs on compatible OpenBSD
# systems of the same architecture.
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
# Prerequisites: a C toolchain, GNU make, autotools, and wget or curl.
# A system SDL2 development package is not needed.
#
# Optional knobs (environment):
#   SDL2_CONFIG=/path/to/sdl2-config   use an existing headless static SDL2
#   SDL_VERSION=2.30.9                 SDL2 release to fetch when building
#   GCC=musl-gcc or CC=musl-gcc        choose the compiler used for SDL/SIMH
#
# Usage: sh build_static.sh [target ...]
#        default targets: pdp6 pdp10-ka pdp10-ki pdp10-kl pdp10-ks

set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
OUT=$ROOT/sims
DEPS=$ROOT/.static-deps                 # local prefix for a headless SDL2
SDL_VERSION=${SDL_VERSION:-2.30.9}

mkdir -p "$OUT"

TARGETS="${*:-pdp6 pdp10-ka pdp10-ki pdp10-kl pdp10-ks}"

# SIMH's makefile requires GNU make.  Prefer the conventional gmake name,
# then accept make only when it identifies itself as GNU make.
if [ -n "$MAKE" ]; then
    BUILD_MAKE=$MAKE
else
    BUILD_MAKE=
    for candidate in gmake make; do
        if command -v "$candidate" >/dev/null 2>&1 &&
           "$candidate" --version 2>/dev/null | grep 'GNU Make' >/dev/null; then
            BUILD_MAKE=$candidate
            break
        fi
    done
fi

if [ -z "$BUILD_MAKE" ] || ! command -v "$BUILD_MAKE" >/dev/null 2>&1; then
    echo "  !! GNU make is required (set MAKE to its command name)" >&2
    exit 1
fi

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
case $JOBS in
    ''|*[!0-9]*|0) JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 2) ;;
esac

if [ -n "$GCC" ]; then
    BUILD_CC=$GCC
elif [ -n "$CC" ]; then
    BUILD_CC=$CC
elif command -v musl-gcc >/dev/null 2>&1; then
    BUILD_CC=musl-gcc
elif command -v x86_64-linux-musl-gcc >/dev/null 2>&1; then
    BUILD_CC=x86_64-linux-musl-gcc
elif command -v cc >/dev/null 2>&1; then
    BUILD_CC=cc
elif command -v gcc >/dev/null 2>&1; then
    BUILD_CC=gcc
else
    echo "  !! no C compiler found (set CC or GCC)" >&2
    exit 1
fi

if ! command -v "$BUILD_CC" >/dev/null 2>&1; then
    echo "  !! requested C compiler not found: $BUILD_CC" >&2
    exit 1
fi

# Derive optional compiler/linker flags from the selected toolchain rather
# than from an OS name or a particular linker implementation.
PROBE_SRC=$OUT/.static-link-probe-$$.c
PROBE_BIN=$OUT/.static-link-probe-$$
printf '%s\n' 'int main(void) { return 0; }' >"$PROBE_SRC"
if ! "$BUILD_CC" "$PROBE_SRC" -static -o "$PROBE_BIN" >/dev/null 2>&1; then
    rm -f "$PROBE_SRC" "$PROBE_BIN"
    echo "  !! $BUILD_CC cannot produce a static executable" >&2
    exit 1
fi
STATIC_LINK_FLAGS=-static
SECTION_FLAGS=
for flag in -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections; do
    if "$BUILD_CC" "$PROBE_SRC" -static "$flag" -o "$PROBE_BIN" >/dev/null 2>&1; then
        case $flag in
            -ffunction-sections|-fdata-sections) SECTION_FLAGS="$SECTION_FLAGS $flag" ;;
            *) STATIC_LINK_FLAGS="$STATIC_LINK_FLAGS $flag" ;;
        esac
    fi
done
rm -f "$PROBE_SRC" "$PROBE_BIN"

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

fetch()
{
    url=$1
    output=$2
    if command -v wget >/dev/null 2>&1; then
        wget -q -O "$output" "$url"
    elif command -v curl >/dev/null 2>&1; then
        curl -fL -o "$output" "$url"
    else
        echo "  !! wget or curl is required to fetch SDL2" >&2
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
elif [ -x "$DEPS/bin/sdl2-config" ] &&
     [ "$(cat "$DEPS/.compiler" 2>/dev/null)" = "$BUILD_CC" ] &&
     [ "$(cat "$DEPS/.source-root" 2>/dev/null)" = "$ROOT" ]; then
    SDL_CONFIG=$DEPS/bin/sdl2-config
else
    echo ">> Building headless static SDL2 $SDL_VERSION into $DEPS"
    mkdir -p "$DEPS/src"
    cd "$DEPS/src"
    tarball="SDL2-$SDL_VERSION.tar.gz"
    [ -f "$tarball" ] || fetch "https://libsdl.org/release/$tarball" "$tarball"
    # Extract unless a complete tree (with configure) is already present.
    [ -f "SDL2-$SDL_VERSION/configure" ] || { rm -rf "SDL2-$SDL_VERSION"; tar xzf "$tarball"; }
    cd "SDL2-$SDL_VERSION"
    # Keep the log outside the source tree: SDL's generated makefiles may
    # remove *.log files there during the build.
    SDL_BUILD_LOG="$DEPS/SDL2-$SDL_VERSION.build.log"
    # Generated dependency files contain absolute paths.  Clear them before
    # reconfiguration so a relocated checkout or prefix rebuilds cleanly.
    if [ -f Makefile ]; then
        if ! "$BUILD_MAKE" clean >"$SDL_BUILD_LOG" 2>&1; then
            # A relocated generated Makefile may be unable to clean itself.
            # These are SDL's generated-output directories, not source.
            rm -rf build gen
        fi
    fi
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
        "$BUILD_MAKE" -j"$JOBS"
    run_logged "SDL2 install" "$SDL_BUILD_LOG" "$BUILD_MAKE" install
    printf '%s\n' "$BUILD_CC" >"$DEPS/.compiler"
    printf '%s\n' "$ROOT" >"$DEPS/.source-root"
    cd "$ROOT"
    SDL_CONFIG=$DEPS/bin/sdl2-config
fi
echo ">> Using SDL2: $SDL_CONFIG"
echo ">> Using compiler: $BUILD_CC"
echo ">> Using GNU make: $BUILD_MAKE"
echo ">> Using static-link flags:$STATIC_LINK_FLAGS"

# --- 2. Build each target, re-linked fully static --------------------------
for t in $TARGETS; do
    echo "=== $t ==="
    rm -f "BIN/$t"
    # Grab the single gcc/cc invocation make would run for this target.
    cmd=$($BUILD_MAKE -n GCC="$BUILD_CC" "$t" 2>/dev/null | grep -E "^($BUILD_CC|gcc|cc) .* -o " | head -1)
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
        | sed -e "s# -o BIN/$t# $SECTION_FLAGS $STATIC_LINK_FLAGS -o $OUT/$t#")
    run_build "$t" "$cmd"
    file "$OUT/$t"
done
echo "=== done: binaries in $OUT ==="

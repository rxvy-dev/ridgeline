#!/usr/bin/env bash
# Ridgeline installer - builds and installs the game on (almost) any Linux distro.
#
#   ./install.sh               install for the current user into ~/.local
#   ./install.sh --system      install system-wide into /usr/local (uses sudo/doas)
#   ./install.sh --prefix DIR  install into DIR
#   ./install.sh --no-deps     skip installing packages with the package manager
#   ./install.sh --bundle-sdl  always build SDL3 from source and bundle it
#   ./install.sh --uninstall   remove a previous install (use the same --prefix/--system)
#
set -euo pipefail

SDL_TAG="release-3.2.24"
APP="ridgeline"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${HOME}/.local"
SYSTEM=0
DEPS=1
BUNDLE=0
UNINSTALL=0
JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

c_red=$'\e[31m'; c_grn=$'\e[32m'; c_yel=$'\e[33m'; c_blu=$'\e[34m'; c_off=$'\e[0m'
[ -t 1 ] || { c_red=""; c_grn=""; c_yel=""; c_blu=""; c_off=""; }
info() { printf '%s==>%s %s\n' "$c_blu" "$c_off" "$*"; }
ok()   { printf '%s ok%s %s\n' "$c_grn" "$c_off" "$*"; }
warn() { printf '%s !!%s %s\n' "$c_yel" "$c_off" "$*" >&2; }
die()  { printf '%serror:%s %s\n' "$c_red" "$c_off" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

usage() { sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
    case "$1" in
        --system) SYSTEM=1; PREFIX="/usr/local" ;;
        --prefix) shift; [ $# -gt 0 ] || die "--prefix needs a directory"; PREFIX="$1" ;;
        --prefix=*) PREFIX="${1#*=}" ;;
        --no-deps) DEPS=0 ;;
        --bundle-sdl) BUNDLE=1 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) usage ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done

# privilege helper (only used for package installs and --system)
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if have sudo; then SUDO="sudo"; elif have doas; then SUDO="doas"; fi
fi
as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"
    elif [ -n "$SUDO" ]; then $SUDO "$@"
    else warn "need root for: $*  (no sudo/doas found)"; return 1
    fi
}
INSTALL_AS=""
if [ "$SYSTEM" -eq 1 ] && [ "$(id -u)" -ne 0 ]; then INSTALL_AS="$SUDO"; fi
if [ "$SYSTEM" -eq 0 ] && [ -e "$PREFIX" ] && [ ! -w "$PREFIX" ]; then INSTALL_AS="$SUDO"; fi

MANIFEST="$PREFIX/share/$APP/install-manifest.txt"

# ------------------------------------------------------------------ uninstall
if [ "$UNINSTALL" -eq 1 ]; then
    [ -f "$MANIFEST" ] || die "no install found in $PREFIX (missing $MANIFEST)"
    info "Removing Ridgeline from $PREFIX"
    while IFS= read -r f || [ -n "$f" ]; do
        [ -n "$f" ] && { $INSTALL_AS rm -f "$f" && echo "  removed $f"; }
    done < "$MANIFEST"
    $INSTALL_AS rm -f "$MANIFEST"
    for d in "$PREFIX/share/$APP/cars"/*/; do [ -d "$d" ] && $INSTALL_AS rmdir "$d" 2>/dev/null; done
    $INSTALL_AS rmdir "$PREFIX/share/$APP/cars" "$PREFIX/share/$APP" "$PREFIX/lib/$APP" 2>/dev/null || true
    have update-desktop-database && $INSTALL_AS update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
    ok "Uninstalled. Your save game is kept in ~/.local/share/rxvy/ridgeline/"
    exit 0
fi

# ------------------------------------------------------------------ distro detection
ID="unknown"; ID_LIKE=""; PRETTY="Linux"
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    ID="${ID:-unknown}"; ID_LIKE="${ID_LIKE:-}"; PRETTY="${PRETTY_NAME:-$ID}"
fi
PM=""
for p in pacman apt-get dnf zypper xbps-install emerge apk eopkg nix-env; do
    if have "$p"; then PM="$p"; break; fi
done
info "Detected: $PRETTY  (package manager: ${PM:-none})"

# Packages: tools, then SDL3 dev package, then what SDL3 needs to build from source.
pkgs_tools=""; pkgs_sdl=""; pkgs_sdl_build=""
case "$PM" in
    pacman)
        pkgs_tools="base-devel cmake git pkgconf"
        pkgs_sdl="sdl3"
        pkgs_sdl_build="libx11 libxext libxrandr libxcursor libxi libxss libxkbcommon wayland wayland-protocols mesa libpulse alsa-lib pipewire libdecor" ;;
    apt-get)
        pkgs_tools="build-essential cmake git pkg-config"
        pkgs_sdl="libsdl3-dev"
        pkgs_sdl_build="libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev libpulse-dev libasound2-dev libpipewire-0.3-dev libdecor-0-dev libudev-dev libdbus-1-dev" ;;
    dnf)
        pkgs_tools="gcc-c++ make cmake git pkgconf-pkg-config"
        pkgs_sdl="SDL3-devel"
        pkgs_sdl_build="libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXScrnSaver-devel libxkbcommon-devel wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGL-devel pulseaudio-libs-devel alsa-lib-devel pipewire-devel libdecor-devel systemd-devel" ;;
    zypper)
        pkgs_tools="gcc-c++ make cmake git pkg-config"
        pkgs_sdl="SDL3-devel"
        pkgs_sdl_build="libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXss-devel libxkbcommon-devel wayland-devel wayland-protocols-devel Mesa-libEGL-devel Mesa-libGL-devel libpulse-devel alsa-devel pipewire-devel libdecor-devel" ;;
    xbps-install)
        pkgs_tools="base-devel cmake git pkg-config"
        pkgs_sdl="SDL3-devel"
        pkgs_sdl_build="libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXScrnSaver-devel libxkbcommon-devel wayland-devel wayland-protocols MesaLib-devel pulseaudio-devel alsa-lib-devel pipewire-devel libdecor-devel" ;;
    emerge)
        pkgs_tools="dev-build/cmake dev-vcs/git virtual/pkgconfig"
        pkgs_sdl="media-libs/libsdl3"
        pkgs_sdl_build="" ;;
    apk)
        pkgs_tools="build-base cmake git pkgconf bash"
        pkgs_sdl="sdl3-dev"
        pkgs_sdl_build="libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxscrnsaver-dev libxkbcommon-dev wayland-dev wayland-protocols mesa-dev pulseaudio-dev alsa-lib-dev pipewire-dev libdecor-dev" ;;
    eopkg)
        pkgs_tools="cmake git pkgconf"
        pkgs_sdl="sdl3-devel"
        pkgs_sdl_build="libx11-devel libxext-devel libxrandr-devel libxcursor-devel libxi-devel libxkbcommon-devel wayland-devel wayland-protocols-devel mesalib-devel pulseaudio-devel alsa-lib-devel pipewire-devel" ;;
esac

pm_install() {
    [ $# -gt 0 ] || return 0
    case "$PM" in
        pacman)       as_root pacman -S --needed --noconfirm "$@" ;;
        apt-get)      as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y "$@" ;;
        dnf)          as_root dnf install -y "$@" ;;
        zypper)       as_root zypper --non-interactive install "$@" ;;
        xbps-install) as_root xbps-install -Sy "$@" ;;
        emerge)       as_root emerge --noreplace --quiet-build "$@" ;;
        apk)          as_root apk add "$@" ;;
        eopkg)        as_root eopkg install -y "$@" ;;
        *)            return 1 ;;
    esac
}
# Install a list one package at a time so a single unknown name doesn't block the rest.
pm_install_each() {
    local p failed=""
    for p in "$@"; do pm_install "$p" >/dev/null 2>&1 || failed="$failed $p"; done
    [ -z "$failed" ] || warn "could not install:$failed"
}

if [ "$DEPS" -eq 1 ]; then
    if [ "$PM" = "nix-env" ]; then
        warn "NixOS detected: run this script inside   nix-shell -p cmake gcc gnumake pkg-config git sdl3"
    elif [ -n "$PM" ]; then
        info "Installing build tools"
        [ "$PM" = "apt-get" ] && { as_root apt-get update -qq || warn "apt-get update failed"; }
        [ "$PM" = "eopkg" ] && { as_root eopkg install -y -c system.devel >/dev/null 2>&1 || true; }
        # shellcheck disable=SC2086
        pm_install $pkgs_tools || pm_install_each $pkgs_tools
        if [ "$BUNDLE" -eq 0 ]; then
            info "Installing SDL3 ($pkgs_sdl)"
            # shellcheck disable=SC2086
            pm_install $pkgs_sdl || warn "SDL3 package not available here - it will be built from source"
        fi
    else
        warn "Unknown package manager - make sure a C++17 compiler, cmake, git and SDL3 are installed"
    fi
fi

# ------------------------------------------------------------------ toolchain checks
CXX_BIN="${CXX:-}"
[ -n "$CXX_BIN" ] || for c in g++ clang++ c++; do have "$c" && { CXX_BIN="$c"; break; }; done
[ -n "$CXX_BIN" ] || die "no C++ compiler found (install g++ or clang)"
have cmake || die "cmake not found"
CMAKE_VER="$(cmake --version | head -n1 | awk '{print $3}')"
ok "compiler: $CXX_BIN, cmake $CMAKE_VER"

# ------------------------------------------------------------------ SDL3
SDL_PREFIX=""
sdl_ok() {
    pkg-config --exists 'sdl3 >= 3.2.0' 2>/dev/null && return 0
    for d in /usr/lib/cmake/SDL3 /usr/lib64/cmake/SDL3 /usr/local/lib/cmake/SDL3 /usr/lib/*/cmake/SDL3; do
        [ -f "$d/SDL3Config.cmake" ] && return 0
    done
    return 1
}
if [ "$BUNDLE" -eq 0 ] && sdl_ok; then
    ok "using system SDL3 $(pkg-config --modversion sdl3 2>/dev/null || true)"
else
    info "Building SDL3 ($SDL_TAG) from source - this takes a minute"
    have git || die "git is needed to download SDL3"
    if [ "$DEPS" -eq 1 ] && [ -n "$pkgs_sdl_build" ]; then
        info "Installing SDL3 build dependencies"
        # shellcheck disable=SC2086
        pm_install $pkgs_sdl_build >/dev/null 2>&1 || pm_install_each $pkgs_sdl_build
    fi
    SRC="$HERE/.deps/SDL"
    SDL_PREFIX="$HERE/.deps/sdl3-install"
    if [ ! -d "$SRC/.git" ]; then
        rm -rf "$SRC"
        git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$SRC"
    fi
    cmake -S "$SRC" -B "$HERE/.deps/sdl3-build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
        -DCMAKE_INSTALL_LIBDIR=lib -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_SHARED=ON -DSDL_STATIC=OFF > "$HERE/.deps/sdl3-configure.log" 2>&1 \
        || die "SDL3 configure failed, see .deps/sdl3-configure.log"
    CACHE="$HERE/.deps/sdl3-build/CMakeCache.txt"
    if ! grep -qE '^SDL_(X11|WAYLAND):BOOL=ON' "$CACHE" || ! grep -q '^SDL_OPENGL:BOOL=ON' "$CACHE"; then
        warn "SDL3 was configured without X11/Wayland or OpenGL support."
        warn "Install your distro's X11/Wayland/Mesa *development* packages and run ./install.sh again:"
        warn "  $pkgs_sdl_build"
        die "cannot build a usable SDL3"
    fi
    cmake --build "$HERE/.deps/sdl3-build" -j "$JOBS" > "$HERE/.deps/sdl3-build.log" 2>&1 || die "SDL3 build failed, see .deps/sdl3-build.log"
    cmake --install "$HERE/.deps/sdl3-build" > /dev/null
    ok "SDL3 built"
fi

# ------------------------------------------------------------------ build the game
info "Building Ridgeline"
CMAKE_ARGS=(-S "$HERE" -B "$HERE/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_CXX_COMPILER="$CXX_BIN")
[ -n "$SDL_PREFIX" ] && CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$SDL_PREFIX")
cmake "${CMAKE_ARGS[@]}" > "$HERE/build-configure.log" 2>&1 || { cat "$HERE/build-configure.log"; die "configure failed"; }
cmake --build "$HERE/build" -j "$JOBS" || die "build failed"
ok "built $HERE/build/$APP"

# ------------------------------------------------------------------ install
info "Installing into $PREFIX"
$INSTALL_AS mkdir -p "$PREFIX/share/$APP"
# cmake --install rewrites the runtime library path to $PREFIX/lib/ridgeline
$INSTALL_AS cmake --install "$HERE/build" --strip > /dev/null || die "install failed"
files=()
while IFS= read -r f || [ -n "$f" ]; do files+=("$f"); done < "$HERE/build/install_manifest.txt"
# point the desktop entry at the absolute binary path (works even if PREFIX/bin is not on PATH)
DESK="$PREFIX/share/applications/ridgeline.desktop"
$INSTALL_AS sed -i "s|^Exec=.*|Exec=$PREFIX/bin/$APP|" "$DESK"
# absolute icon path too, so launchers show it even before the icon cache is refreshed
$INSTALL_AS sed -i "s|^Icon=.*|Icon=$PREFIX/share/icons/hicolor/256x256/apps/$APP.png|" "$DESK"
# local car packs (./cars/<name>/ with car.ini + model.rcar) - never committed, installed if present
if [ -d "$HERE/cars" ]; then
    for d in "$HERE"/cars/*/; do
        [ -f "${d}car.ini" ] && [ -f "${d}model.rcar" ] || continue
        name="$(basename "$d")"
        $INSTALL_AS mkdir -p "$PREFIX/share/$APP/cars/$name"
        for f in "${d}"*; do
            $INSTALL_AS cp "$f" "$PREFIX/share/$APP/cars/$name/"
            files+=("$PREFIX/share/$APP/cars/$name/$(basename "$f")")
        done
        ok "installed car pack: $name"
    done
fi
if [ -n "$SDL_PREFIX" ]; then
    $INSTALL_AS mkdir -p "$PREFIX/lib/$APP"
    for so in "$SDL_PREFIX"/lib/libSDL3.so*; do
        $INSTALL_AS cp -P "$so" "$PREFIX/lib/$APP/"
        files+=("$PREFIX/lib/$APP/$(basename "$so")")
    done
    ok "bundled SDL3 into $PREFIX/lib/$APP"
fi
printf '%s\n' "${files[@]}" | $INSTALL_AS tee "$MANIFEST" >/dev/null
have update-desktop-database && $INSTALL_AS update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
have gtk-update-icon-cache && $INSTALL_AS gtk-update-icon-cache -q -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
have xdg-icon-resource && xdg-icon-resource forceupdate 2>/dev/null || true

# sanity check: the binary resolves its libraries
if have ldd && ldd "$PREFIX/bin/$APP" | grep -q "not found"; then
    ldd "$PREFIX/bin/$APP" | grep "not found" >&2
    die "the installed binary is missing libraries"
fi

echo
ok "Ridgeline installed!"
echo "   Run it from your app menu, or with:  $PREFIX/bin/$APP"
case ":$PATH:" in
    *":$PREFIX/bin:"*) echo "   ...or just type:  $APP" ;;
    *) warn "$PREFIX/bin is not on your PATH. Add this to your shell config:  export PATH=\"$PREFIX/bin:\$PATH\"" ;;
esac
echo "   Uninstall with:  ./install.sh --uninstall$( [ "$SYSTEM" -eq 1 ] && echo ' --system')"

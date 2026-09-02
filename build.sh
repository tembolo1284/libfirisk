#!/usr/bin/env bash
#
# libfirisk build driver.
#
#   ./build.sh              native, python, and every test
#   ./build.sh native       configure + build the C/C++ library
#   ./build.sh ctest        native tests only (assumes a build exists)
#   ./build.sh cpp          native + ctest
#   ./build.sh venv         create the virtualenv and install deps
#   ./build.sh cffi         CFFI tests against the built .so
#   ./build.sh wheel        build and install the nanobind extension
#   ./build.sh pytest       binding tests (assumes the wheel is installed)
#   ./build.sh python       venv + cffi + wheel + pytest
#   ./build.sh asan         build and test under AddressSanitizer
#   ./build.sh both         run the full cpp stage under gcc and clang
#   ./build.sh examples     run the example programs
#   ./build.sh presets      list the available presets
#   ./build.sh clean        remove build trees and the venv
#
# Compiler and build type:
#   ./build.sh --clang cpp            use clang instead of gcc
#   ./build.sh --gcc --release cpp    gcc, RelWithDebInfo
#   ./build.sh --static native        build a static library
#   PRESET=clang-asan ./build.sh cpp  name a preset outright
#
# Other environment:
#   PYTHON=python3.12 ./build.sh venv        pick an interpreter
#   JOBS=4 ./build.sh native                 limit parallelism

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PYTHON="${PYTHON:-python3}"
VENV="${VENV:-$ROOT/.venv}"
JOBS="${JOBS:-}"

# --------------------------------------------------------------------
# output
# --------------------------------------------------------------------

if [ -t 1 ]; then
    BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'
    YELLOW=$'\033[33m'; RESET=$'\033[0m'
else
    BOLD=""; RED=""; GREEN=""; YELLOW=""; RESET=""
fi

stage() { printf '\n%s==> %s%s\n' "$BOLD" "$1" "$RESET"; }
info()  { printf '    %s\n' "$1"; }
warn()  { printf '%s    %s%s\n' "$YELLOW" "$1" "$RESET"; }
die()   { printf '%s==> %s%s\n' "$RED" "$1" "$RESET" >&2; exit 1; }
ok()    { printf '%s    %s%s\n' "$GREEN" "$1" "$RESET"; }

require() {
    command -v "$1" >/dev/null 2>&1 || die "$1 not found; $2"
}

# --------------------------------------------------------------------
# compiler and build type selection
# --------------------------------------------------------------------

COMPILER="${COMPILER:-gcc}"
BUILD_TYPE="debug"
PRESET_OVERRIDE="${PRESET:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --gcc)      COMPILER="gcc";   shift ;;
        --clang)    COMPILER="clang"; shift ;;
        --debug)    BUILD_TYPE="debug";   shift ;;
        --release)  BUILD_TYPE="release"; shift ;;
        --static)   BUILD_TYPE="static";  shift ;;
        --asan)     BUILD_TYPE="asan";    shift ;;
        --tsan)     BUILD_TYPE="tsan";    shift ;;
        --preset)   PRESET_OVERRIDE="${2:-}"; shift 2 ;;
        --)         shift; break ;;
        -*)         die "unknown option: $1 (try --help)" ;;
        *)          break ;;
    esac
done

if [ -n "$PRESET_OVERRIDE" ]; then
    PRESET="$PRESET_OVERRIDE"
else
    PRESET="${COMPILER}-${BUILD_TYPE}"
fi

BUILD_DIR="$ROOT/build/$PRESET"

# The compiler the preset actually names, so a PRESET override still
# gets its toolchain checked.
preset_compiler() {
    case "$PRESET" in
        clang-*) echo "clang" ;;
        *)       echo "gcc" ;;
    esac
}

check_toolchain() {
    local which_cc
    which_cc="$(preset_compiler)"

    if [ "$which_cc" = "clang" ]; then
        require clang "install clang, or use --gcc"
        require clang++ "install clang++, or use --gcc"
    else
        require gcc "install gcc, or use --clang"
        require g++ "install g++, or use --clang"
    fi
}

# --------------------------------------------------------------------
# stages
# --------------------------------------------------------------------

do_presets() {
    stage "Available presets"
    cmake --list-presets
}

do_native() {
    stage "Configuring ($PRESET)"
    require cmake "install cmake >= 3.21"
    check_toolchain

    cmake --preset "$PRESET"

    stage "Building native library ($PRESET)"
    if [ -n "$JOBS" ]; then
        cmake --build --preset "$PRESET" -- -j "$JOBS"
    else
        cmake --build --preset "$PRESET"
    fi

    local lib
    lib="$(find "$BUILD_DIR/src" -maxdepth 1 \
              \( -name 'libfirisk.so*' -o -name 'libfirisk.dylib' \
                 -o -name 'libfirisk.a' \) 2>/dev/null | head -n1)"

    [ -n "$lib" ] || die "build finished but no library found in $BUILD_DIR/src"
    ok "built $(basename "$lib")"
}

do_ctest() {
    stage "Running native tests ($PRESET)"
    [ -d "$BUILD_DIR" ] || die "no build at $BUILD_DIR; run ./build.sh native"

    ctest --preset "$PRESET"
}

do_venv() {
    stage "Preparing virtualenv"
    require "$PYTHON" "set PYTHON to an interpreter that exists"

    if [ ! -d "$VENV" ]; then
        info "creating $VENV"
        "$PYTHON" -m venv "$VENV"
    else
        info "reusing $VENV"
    fi

    # shellcheck disable=SC1091
    source "$VENV/bin/activate"

    python -m pip install --quiet --upgrade pip

    info "installing build and test dependencies"
    python -m pip install --quiet \
        cffi pytest numpy \
        "scikit-build-core>=0.9" "nanobind>=2.0"

    ok "$(python --version) at $VENV"
}

activate() {
    [ -d "$VENV" ] || die "no virtualenv; run ./build.sh venv"
    # shellcheck disable=SC1091
    source "$VENV/bin/activate"
}

do_cffi() {
    stage "Running CFFI tests (against $PRESET)"
    activate

    local lib
    lib="$(find "$BUILD_DIR/src" -maxdepth 1 \
              \( -name 'libfirisk.so' -o -name 'libfirisk.dylib' \) \
              2>/dev/null | head -n1)"

    if [ -z "$lib" ]; then
        die "no shared library in $BUILD_DIR/src. A static preset cannot be
    tested through CFFI; use a shared one, or run ./build.sh native first."
    fi

    # Explicit, so a stale library from another preset is never picked up.
    FIRISK_LIBRARY="$lib" python -m pytest tests/python -q
}

do_wheel() {
    stage "Building the Python extension"
    activate

    local pkg="$ROOT/bindings/python"

    for required in pyproject.toml CMakeLists.txt firisk_ext.cpp \
                    firisk/__init__.py; do
        [ -f "$pkg/$required" ] \
            || die "missing $pkg/$required — check the bindings layout"
    done

    # The wheel builds its own static copy of the library, with the
    # compiler chosen here rather than by the preset.
    local cc cxx
    if [ "$(preset_compiler)" = "clang" ]; then
        cc="clang"; cxx="clang++"
    else
        cc="gcc"; cxx="g++"
    fi

    info "using $cxx"
    CC="$cc" CXX="$cxx" python -m pip install --quiet --no-build-isolation \
        --editable ./bindings/python

    python -c 'import firisk; print("    firisk", firisk.version())'
}

do_pytest() {
    stage "Running binding tests"
    activate

    python -c 'import firisk' 2>/dev/null \
        || die "firisk is not importable; run ./build.sh wheel"

    python -m pytest bindings/python/tests -q
}

do_examples() {
    stage "Running examples ($PRESET)"

    local found=0
    for exe in "$BUILD_DIR"/examples/fir_*; do
        [ -x "$exe" ] || continue
        found=1
        printf '\n  %s%s%s\n' "$BOLD" "$(basename "$exe")" "$RESET"
        "$exe"
    done

    [ "$found" = 1 ] || warn "no examples built in $BUILD_DIR"
}

do_asan() {
    local saved="$PRESET"
    PRESET="$(preset_compiler)-asan"
    BUILD_DIR="$ROOT/build/$PRESET"

    do_native
    do_ctest

    PRESET="$saved"
    BUILD_DIR="$ROOT/build/$PRESET"
}

# Full native cycle under both toolchains. Different compilers disagree
# about warnings and UB, so building under both catches more than either
# alone — and it is the cheapest cross-check available.
do_both() {
    local saved_preset="$PRESET"
    local saved_dir="$BUILD_DIR"
    local failed=""

    for cc in gcc clang; do
        if ! command -v "$cc" >/dev/null 2>&1; then
            warn "$cc not installed, skipping"
            continue
        fi

        PRESET="${cc}-${BUILD_TYPE}"
        BUILD_DIR="$ROOT/build/$PRESET"

        if do_native && do_ctest; then
            ok "$cc: green"
        else
            failed="$failed $cc"
        fi
    done

    PRESET="$saved_preset"
    BUILD_DIR="$saved_dir"

    [ -z "$failed" ] || die "failed under:$failed"

    stage "Summary"
    ok "green under every available toolchain"
}

do_clean() {
    stage "Cleaning"

    for target in "$ROOT/build" "$VENV" \
                  "$ROOT/bindings/python/build" \
                  "$ROOT/bindings/python/firisk.egg-info"; do
        if [ -e "$target" ]; then
            info "removing ${target#"$ROOT"/}"
            rm -rf "$target"
        fi
    done

    find "$ROOT" -name '__pycache__' -type d -prune -exec rm -rf {} + \
        2>/dev/null || true
    find "$ROOT" -name '.pytest_cache' -type d -prune -exec rm -rf {} + \
        2>/dev/null || true

    ok "clean"
}

# --------------------------------------------------------------------
# composites
# --------------------------------------------------------------------

do_python() {
    do_venv
    do_cffi
    do_wheel
    do_pytest
}

do_all() {
    do_native
    do_ctest
    do_venv
    do_cffi
    do_wheel
    do_pytest

    stage "Summary"
    ok "native library, C smoke test, CFFI suite, and bindings all green"
    info "preset: $PRESET"
}

usage() {
    sed -n '3,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# --------------------------------------------------------------------
# dispatch
# --------------------------------------------------------------------

case "${1:-all}" in
    native)   do_native ;;
    ctest)    do_ctest ;;
    cpp)      do_native; do_ctest ;;
    venv)     do_venv ;;
    cffi)     do_cffi ;;
    wheel)    do_wheel ;;
    pytest)   do_pytest ;;
    python)   do_python ;;
    examples) do_examples ;;
    asan)     do_asan ;;
    both)     do_both ;;
    presets)  do_presets ;;
    clean)    do_clean ;;
    all)      do_all ;;
    -h|--help|help) usage ;;
    *)        die "unknown stage: $1 (try --help)" ;;
esac

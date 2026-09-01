#!/usr/bin/env bash
#
# libfirisk build driver.
#
#   ./build.sh              native, python, and every test
#   ./build.sh native       configure + build the C/C++ library
#   ./build.sh ctest        native tests only (assumes a build exists)
#   ./build.sh venv         create the virtualenv and install deps
#   ./build.sh cffi         CFFI tests against the built .so
#   ./build.sh wheel        build and install the nanobind extension
#   ./build.sh pytest       binding tests (assumes the wheel is installed)
#   ./build.sh python       venv + cffi + wheel + pytest
#   ./build.sh asan         reconfigure under ASan and run the native tests
#   ./build.sh examples     run the example programs
#   ./build.sh clean        remove build trees and the venv
#
# Environment:
#   PRESET=gcc-release ./build.sh native     pick a CMake preset
#   PYTHON=python3.12 ./build.sh venv        pick an interpreter
#   JOBS=4 ./build.sh native                 limit parallelism

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

PRESET="${PRESET:-gcc-debug}"
PYTHON="${PYTHON:-python3}"
VENV="${VENV:-$ROOT/.venv}"
JOBS="${JOBS:-}"

BUILD_DIR="$ROOT/build/$PRESET"

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
# stages
# --------------------------------------------------------------------

do_native() {
    stage "Configuring ($PRESET)"
    require cmake "install cmake >= 3.21"

    cmake --preset "$PRESET"

    stage "Building native library"
    if [ -n "$JOBS" ]; then
        cmake --build --preset "$PRESET" -- -j "$JOBS"
    else
        cmake --build --preset "$PRESET"
    fi

    local lib
    lib="$(find "$BUILD_DIR/src" -maxdepth 1 -name 'libfirisk.so*' \
              -o -maxdepth 1 -name 'libfirisk.dylib' \
              -o -maxdepth 1 -name 'libfirisk.a' 2>/dev/null | head -n1)"

    [ -n "$lib" ] || die "build finished but no library found in $BUILD_DIR/src"
    ok "built $(basename "$lib")"
}

do_ctest() {
    stage "Running native tests"
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

# Every python stage needs the venv active. Sourcing it repeatedly is
# harmless, and it means each stage runs standalone.
activate() {
    [ -d "$VENV" ] || die "no virtualenv; run ./build.sh venv"
    # shellcheck disable=SC1091
    source "$VENV/bin/activate"
}

do_cffi() {
    stage "Running CFFI tests"
    activate

    local lib="$BUILD_DIR/src/libfirisk.so"
    if [ ! -f "$lib" ]; then
        lib="$(find "$BUILD_DIR/src" -maxdepth 1 -name 'libfirisk.so' \
                  -o -maxdepth 1 -name 'libfirisk.dylib' | head -n1)"
    fi

    [ -n "$lib" ] && [ -f "$lib" ] \
        || die "no shared library at $BUILD_DIR/src; run ./build.sh native"

    # testhelpers finds the library itself, but being explicit stops it
    # picking up a stale one from a different preset.
    FIRISK_LIBRARY="$lib" python -m pytest tests/python -q
}

do_wheel() {
    stage "Building the Python extension"
    activate

    # Editable installs rebuild on import, which is what you want while
    # iterating on the bindings. --no-build-isolation reuses the venv's
    # nanobind rather than downloading another copy per build.
    python -m pip install --quiet --no-build-isolation \
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
    stage "Running examples"

    for exe in "$BUILD_DIR"/examples/fir_*; do
        [ -x "$exe" ] || continue
        printf '\n  %s%s%s\n' "$BOLD" "$(basename "$exe")" "$RESET"
        "$exe"
    done
}

do_asan() {
    stage "Building under ASan"
    require clang "install clang for the sanitizer preset"

    cmake --preset clang-asan
    cmake --build --preset clang-asan

    stage "Running native tests under ASan"
    ctest --preset clang-asan
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
}

usage() {
    sed -n '3,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# --------------------------------------------------------------------
# dispatch
# --------------------------------------------------------------------

case "${1:-all}" in
    native)   do_native ;;
    ctest)    do_ctest ;;
    venv)     do_venv ;;
    cffi)     do_cffi ;;
    wheel)    do_wheel ;;
    pytest)   do_pytest ;;
    python)   do_python ;;
    examples) do_examples ;;
    asan)     do_asan ;;
    clean)    do_clean ;;
    all)      do_all ;;
    cpp)      do_native; do_ctest ;;
    -h|--help|help) usage ;;
    *)        die "unknown stage: $1 (try --help)" ;;
esac

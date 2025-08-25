#!/usr/bin/env bash

set -x
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"


el__build() {
    # Determine build context: Docker (default) or local emsdk (LOCAL_BUILD=1)
    local IS_LOCAL="${LOCAL_BUILD:-}"
    local BUILD_ROOT
    local SRC_DIR

    if [[ -n "$IS_LOCAL" ]]; then
        BUILD_ROOT="$ROOT_DIR/.wasm-build"
        SRC_DIR="$ROOT_DIR"
    else
        BUILD_ROOT="/elembuild"
        SRC_DIR="/src"
    fi

    mkdir -p "$BUILD_ROOT/wasm/"
    pushd "$BUILD_ROOT/wasm/"

    # Minimal injection for external modules and C++ standard
    local CXXFLAGS_EXTRA="-O3 -std=c++20"
    if [[ -n "${EXTERNAL_MODULES}" ]]; then
        CXXFLAGS_EXTRA+=" -DEXTERNAL_MODULES"
    fi
    if [[ -n "${EXTERNAL_INCLUDE}" ]]; then
        CXXFLAGS_EXTRA+=" -I${EXTERNAL_INCLUDE}"
    fi

    ELEM_BUILD_ASYNC="${ELEM_BUILD_ASYNC:-0}" emcmake cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DONLY_BUILD_WASM=ON \
        -DCMAKE_CXX_FLAGS="${CXXFLAGS_EXTRA}" \
        "$SRC_DIR"

    # Parallel build with fallback processor count detection
    local JOBS
    JOBS=$(command -v getconf >/dev/null 2>&1 && getconf _NPROCESSORS_ONLN 2>/dev/null)
    if [[ -z "$JOBS" ]]; then
        JOBS=$(command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu 2>/dev/null)
    fi
    if [[ -z "$JOBS" ]]; then
        JOBS=4
    fi
    emmake make -j"$JOBS"

    mkdir -p "$SRC_DIR/build/out/"
    cp "$BUILD_ROOT/wasm/wasm/elementary-wasm.js" "$SRC_DIR/build/out/elementary-wasm.js"

    popd
}

el__main() {
    # If the first positional argument matches one of the namespaced
    # subcommands above, pop the first positional argumen off the list
    # and invoke the subcommand with the remainder.
    if declare -f "el__${1//-/_}" > /dev/null; then
        fn="el__${1//-/_}"
        shift;
        "$fn" "$@"
    else
        # Else we're running our top-level main, for which we, by default, invoke
        # the build command from within an emscripten/emsdk docker container.
        local OUTPUT_FILENAME=""
        local ELEM_BUILD_ASYNC=0
        local FORCE_LOCAL=0

        while getopts alo: opt; do
            case $opt in
                o)  OUTPUT_FILENAME="$OPTARG";;
                a)  ELEM_BUILD_ASYNC=1;;
                l)  FORCE_LOCAL=1;;
            esac
        done

        shift "$((OPTIND - 1))"

        if [ -z "$OUTPUT_FILENAME" ]; then
            echo "Error: where are we outputting to?"
            exit 1
        fi

        if [[ "$FORCE_LOCAL" -eq 1 ]] || command -v emcmake >/dev/null 2>&1; then
          # Build using local emsdk toolchain
          LOCAL_BUILD=1 \
          ELEM_BUILD_ASYNC="$ELEM_BUILD_ASYNC" \
          EXTERNAL_MODULES="$EXTERNAL_MODULES" \
          EXTERNAL_INCLUDE="$EXTERNAL_INCLUDE" \
          "$ROOT_DIR/scripts/build-wasm.sh" build
        else
          # Fallback to Dockerized build
          docker run \
            -v $(pwd):/src \
            ${EXTERNAL_DIR:+-v ${EXTERNAL_DIR}:${EXTERNAL_DIR}} \
            ${EXTERNAL_INCLUDE:+-v ${EXTERNAL_INCLUDE}:${EXTERNAL_INCLUDE}} \
            --env ELEM_BUILD_ASYNC="$ELEM_BUILD_ASYNC" \
            --env EXTERNAL_MODULES="$EXTERNAL_MODULES" \
            --env EXTERNAL_INCLUDE="$EXTERNAL_INCLUDE" \
            docker.io/emscripten/emsdk:3.1.52 \
            ./scripts/build-wasm.sh build
        fi

        # Copy out the resulting file
        cp $ROOT_DIR/build/out/elementary-wasm.js $OUTPUT_FILENAME
    fi
}

el__main "$@"

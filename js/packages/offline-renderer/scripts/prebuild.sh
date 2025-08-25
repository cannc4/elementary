#!/usr/bin/env bash

set -x
set -e

ROOT_DIR="$(git rev-parse --show-toplevel)"
CURRENT_DIR="$(pwd)"


pushd "$ROOT_DIR"
# Prefer local emsdk if available to avoid Docker; fallback to Docker otherwise
if command -v emcmake >/dev/null 2>&1 || [[ -n "$LOCAL_BUILD" ]]; then
  ./scripts/build-wasm.sh -l -a -o "$CURRENT_DIR/elementary-wasm.cjs"
else
  ./scripts/build-wasm.sh -a -o "$CURRENT_DIR/elementary-wasm.cjs"
fi
popd

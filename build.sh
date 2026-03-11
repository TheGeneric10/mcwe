#!/usr/bin/env bash
set -euo pipefail

mkdir -p dist

emcc src/main.cpp \
  -std=c++20 \
  -O2 \
  -s WASM=1 \
  -s MIN_WEBGL_VERSION=2 \
  -s MAX_WEBGL_VERSION=2 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -o dist/mcwe.js

echo "Build complete: dist/mcwe.js + dist/mcwe.wasm"

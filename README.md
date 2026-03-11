# Minecraft Web Edition Clone Prototype

This repository now contains a minimal C++ + WebAssembly WebGL prototype that renders a rotating 3D cube in `index.html`.

## Build

Requires [Emscripten](https://emscripten.org/docs/getting_started/downloads.html):

```bash
./build.sh
```

## Run locally

Serve with any static file server from this directory (example with Python):

```bash
python3 -m http.server 8080
```

Then open `http://localhost:8080`.

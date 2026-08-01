# RVEMU browser dashboard

This directory contains the thin web interface for the C++ RV32IM emulator.
Architectural execution, ELF validation, hosted syscalls, traps, cache modeling,
branch prediction, and cycle estimation remain in `rvemu_core`. The browser
layer calls a small C API compiled with Emscripten and renders its snapshots.

## Local development

From this directory:

```sh
npm install
npm run build:demo
npm run build:wasm
npm run dev
```

The WebAssembly build requires CMake and Emscripten. The ordinary site build
uses the checked-in `public/wasm` artifacts so deployment does not need a C++
toolchain.

## Validation

```sh
npm test
npm run lint
npx tsc --noEmit
```

The root C++ test suite also exercises the browser bridge against the bundled
demo ELF.

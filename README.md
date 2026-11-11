# EmissionControl2-Next

A fork of [EmissionControl2](https://github.com/EmissionControl2/EmissionControl2), Curtis Roads' granular synthesis instrument, adding DAW plugin support.

## What this adds

Plugin builds for the iPlug2 targets (VST3, AU, AUv3, AAX and CLAP) plus the original standalone application, on macOS, Windows and Linux.

The granular engine is unchanged. The new layers give it MIDI note input, polyphonic voices and pitch tracking, while the original free-running granular mode stays available.

## Upstream bugs

Two defects in the pinned dependencies stop the project building with current tooling:

- `allolib/src/ui/al_Composition.cpp` uses `std::chrono::high_resolution_clock` without including
  `<chrono>`, which older MSVC headers pulled in transitively.
- `oscpack/osc/OscHostEndianness.h` fails to compile unless an endianness macro is defined.

Both are worked around with compiler defines in the build, and should be fixed upstream rather than carried here.

Upstream also declares `cmake_minimum_required(VERSION 3.0)` (Gamma declares 2.8), which CMake 4
rejects; the build passes `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.

## Building

```bash
tools\bootstrap.ps1
cmake -S ecSource -B ecSource\build\release -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
  -DCMAKE_CXX_FLAGS="/FIchrono /wd4244 /DOSC_HOST_LITTLE_ENDIAN" `
  -DSNDFILE_LIBRARY=<repo>/build-deps/libsndfile/lib/sndfile.lib `
  -DSNDFILE_INCLUDE_DIR=<repo>/build-deps/libsndfile/include
cmake --build ecSource\build\release --config Release
```

`tools\bootstrap.ps1` checks out the pinned submodules, applies the diffs in `patches/`, and builds
`nfd.lib`. It is safe to run repeatedly.

libsndfile is not vendored. Put the win64 package's `sndfile.lib`, `sndfile.h` and `sndfile.dll` under
`build-deps/libsndfile/`, and copy the DLL again as `libsndfile-1.dll`, the name EC2's
`CMakeLists.txt` copies.

### Engine tests:

```bash
tests\build_smoke.cmd smoke_ec2.cpp
tests\build_smoke.cmd bench_voices.cpp bench.exe
tests\build_prog.cmd stress.exe tests\adapter_stress.cpp src\engine\EC2Adapter.cpp
```

## Repository layout

```
ecSource/            upstream EC2 application
externalResources/   upstream resources
docs/                upstream documentation
src/engine/          adapter between the engine and the plugin layer
src/instrument/      note-to-cloud voice, voice pool
tests/               engine tests and build scripts
patches/             build fixes for current CMake and MSVC
tools/               bootstrap and nativefiledialog build scripts
```

## Tracking upstream

`master` mirrors upstream and is never committed to; `next` carries this work.

```bash
git fetch upstream
git merge upstream/master
```
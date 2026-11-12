# EmissionControl2-Next

A fork of [EmissionControl2](https://github.com/EmissionControl2/EmissionControl2), Curtis Roads' granular synthesis instrument, adding DAW plugin support.

## What this adds

Plugin builds for the iPlug2 targets (VST3, AU, AUv3, AAX and CLAP) plus the original standalone application, on macOS, Windows and Linux.

The granular engine is unchanged. The new layers give it MIDI note input, polyphonic voices and pitch tracking, while the original free-running granular mode stays available.

Status: pre-alpha but playable. VST3, CLAP and the standalone app all build and run. All seventy-one EC2 parameters are exposed, including modulation depths and the six LFOs.

What is done and what is next is tracked in [ROADMAP.md](ROADMAP.md).

## Upstream bugs

Two defects in the pinned dependencies stop the project building with current tooling:

- `allolib/src/ui/al_Composition.cpp` uses `std::chrono::high_resolution_clock` without including
  `<chrono>`, which older MSVC headers pulled in transitively.
- `oscpack/osc/OscHostEndianness.h` fails to compile unless an endianness macro is defined.

Both are worked around with compiler defines in the build, and should be fixed upstream rather than carried here.

Upstream also declares `cmake_minimum_required(VERSION 3.0)` (Gamma declares 2.8), which CMake 4
rejects; the build passes `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.

## Building

```powershell
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

### The plugin

The same CMake builds the plugin targets. From the repository root:

```powershell
tools\bootstrap.ps1
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
  -DFETCHCONTENT_SOURCE_DIR_WIL=<repo>/vendor/wil `
  -DSNDFILE_LIBRARY=<libsndfile>/lib/sndfile.lib `
  -DSNDFILE_INCLUDE_DIR=<libsndfile>/include
cmake --build build --config Release --target EC2Next-vst3 EC2Next-clap EC2Next-app
```

Artifacts land in `build\out`: `EC2Next.vst3`, `EC2Next.clap` and `EC2Next.exe`, each with `sndfile.dll` beside it. The DLL is delay-loaded from the plugin's own directory, so a host's DLL search order cannot break it.

The VST3 SDK, CLAP SDK and CLAP helpers are pinned submodules under `vendor/`, so the build needs nothing from outside the repository.

### Engine tests

```powershell
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

```powershell
git fetch upstream
git merge upstream/master
```

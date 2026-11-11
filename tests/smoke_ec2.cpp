// smoke_ec2.cpp, Milestone-1 smoke test for the EC2 granular engine (audit §F.1–F.4).
//
// Purpose: prove that ecSynth can be instantiated and driven *outside* al::App -
// no GUI, no audio device, no ImGui loop, and that it produces finite, non-silent
// audio when fed a sample. This is the seam the whole plugin plan depends on.
//
// Build: tests\build_smoke.cmd      Run: tests\build\smoke.exe [path\to\sample.wav]
//
// No upstream file is included or modified beyond the headers already public.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "ecSynth.h"
#include "Gamma/Gamma.h"

namespace {
constexpr int kBlockFrames = 512;
constexpr double kSampleRate = 48000.0;
constexpr int kBlocks = 100;            // 100 * 512 / 48000  ~= 1.07 s
constexpr float kGrainRateHz = 100.0f;  // default is 1 Hz, far too sparse for a short test
}  // namespace

int main(int argc, char **argv) {
  const std::string samplePath =
    argc > 1 ? argv[1]
             : "C:\\Dev\\GitHub\\EmissionControl2-Next\\externalResources\\samples\\440sine48k.wav";

  // --- F.1: instantiate the engine -----------------------------------------
  al::AudioIOData io;
  io.channelsIn(0);
  io.channelsOut(2);
  io.framesPerBuffer(kBlockFrames);
  io.framesPerSecond(kSampleRate);

  gam::sampleRate(kSampleRate);  // Gamma global, mirrors ecInterface.cpp:144

  ecSynth engine;
  // Order matters: ecSynth::initialize() copies AudioChanIndex into PolySynth's
  // channel map, and AudioChanIndex is uninitialized until setOutChannels() runs.
  // The app calls this from onInit() (ecInterface.cpp:135/138); a host must too.
  engine.setOutChannels(0, 2);
  engine.initialize(&io);

  if (!engine.loadSoundFileOffline(samplePath)) {
    std::fprintf(stderr, "FAIL: could not load '%s'\n", samplePath.c_str());
    return 2;
  }
  engine.ECParameters[consts::GRAIN_RATE]->setParam(kGrainRateHz);

  // --- F.2/F.3: supply buffers and process --------------------------------
  double sumSquares = 0.0;
  float peak = 0.0f;
  long nonFinite = 0;
  long long finiteSamples = 0;

  for (int block = 0; block < kBlocks; ++block) {
    io.zeroOut();   // onProcess *adds* into the buffers; zeroing is mandatory
    io.frame(0);
    engine.onProcess(io);

    for (int ch = 0; ch < 2; ++ch) {
      const float *buf = io.outBuffer(ch);
      for (int i = 0; i < kBlockFrames; ++i) {
        const float v = buf[i];
        if (!std::isfinite(v)) {
          ++nonFinite;
          continue;
        }
        peak = std::max(peak, std::fabs(v));
        sumSquares += static_cast<double>(v) * v;
        ++finiteSamples;
      }
    }
  }

  // --- F.4: report what came back -----------------------------------------
  const double rms = finiteSamples ? std::sqrt(sumSquares / finiteSamples) : 0.0;
  std::printf("sample      : %s\n", samplePath.c_str());
  std::printf("blocks      : %d (%d frames each, %.0f Hz)\n", kBlocks, kBlockFrames, kSampleRate);
  std::printf("grains      : %d emitted by the scheduler\n", engine.grainCounter);
  std::printf("active      : %d grain voices still running\n", engine.getNumActiveVoices());
  std::printf("peak        : %.6f\n", peak);
  std::printf("rms         : %.6f\n", rms);
  std::printf("non-finite  : %ld\n", nonFinite);

  const bool finiteOk = (nonFinite == 0);
  const bool audible = (peak > 1e-4f);
  const bool grained = (engine.grainCounter > 0);

  std::printf("RESULT      : %s\n", (finiteOk && audible && grained) ? "PASS" : "FAIL");
  if (!finiteOk) std::printf("  -> non-finite samples in output\n");
  if (!audible) std::printf("  -> output is silent (peak %.6f)\n", peak);
  if (!grained) std::printf("  -> scheduler emitted no grains\n");

  return (finiteOk && audible && grained) ? 0 : 1;
}
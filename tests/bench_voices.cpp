// bench_voices.cpp, how many ecSynth instances can run at once? (audit §F.6.1, §F.7)
//
// The open question in the integration plan: if one MIDI note owns one whole
// ecSynth (a grain cloud with its own scanner, 6 LFOs and 2048 grains), what does
// N voices cost? This measures wall-clock time for N engines sharing one process
// block, and reports CPU% where 100% == exactly real time on one core.
//
// Build: tests\build_smoke.cmd bench_voices.cpp bench.exe
// Run:   bench.exe [sample.wav] [n1 n2 n3 ...]     default: 1 4 8

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "ecSynth.h"
#include "Gamma/Gamma.h"

namespace {
constexpr int kBlockFrames = 512;
constexpr double kSampleRate = 48000.0;
constexpr int kBlocks = 200;       // ~2.13 s of audio per timed run
constexpr int kWarmupBlocks = 50;
constexpr int kRepeats = 3;
constexpr float kGrainRateHz = 100.0f;

double secondsFor(int blocks) { return blocks * kBlockFrames / kSampleRate; }
}  // namespace

int main(int argc, char **argv) {
  const std::string samplePath =
    argc > 1 ? argv[1]
             : "C:\\Dev\\GitHub\\EmissionControl2-Next\\externalResources\\samples\\440sine48k.wav";

  std::vector<int> counts;
  for (int i = 2; i < argc; ++i) counts.push_back(std::atoi(argv[i]));
  if (counts.empty()) counts = {1, 4, 8};

  // One block of audio shared by every engine, exactly as a plugin would share it.
  al::AudioIOData io;
  io.channelsIn(0);
  io.channelsOut(2);
  io.framesPerBuffer(kBlockFrames);
  io.framesPerSecond(kSampleRate);

  gam::sampleRate(kSampleRate);  // Gamma global, set once

  std::printf("block %d frames @ %.0f Hz, %d blocks/run (~%.2f s), %d repeats\n",
              kBlockFrames, kSampleRate, kBlocks, secondsFor(kBlocks), kRepeats);
  std::printf("%-6s %-10s %-10s %-9s %-12s %-10s %s\n",
              "voices", "wall(ms)", "audio(ms)", "CPU%", "per-voice%", "grains", "peak");

  for (int n : counts) {
    std::vector<std::unique_ptr<ecSynth>> engines;
    engines.reserve(static_cast<size_t>(n));

    const auto buildStart = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
      auto engine = std::unique_ptr<ecSynth>(new ecSynth());
      engine->setOutChannels(0, 2);  // must precede initialize(); see audit §C.5.2
      engine->initialize(&io);
      if (!engine->loadSoundFileOffline(samplePath)) {
        std::fprintf(stderr, "FAIL: could not load '%s'\n", samplePath.c_str());
        return 2;
      }
      engine->ECParameters[consts::GRAIN_RATE]->setParam(kGrainRateHz);
      engines.push_back(std::move(engine));
    }
    const double buildMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();

    // One render pass over every engine, accumulating into the shared buffers.
    // io.frame(0) must be re-set before EACH engine: ecSynth::onProcess begins its
    // scheduling loop with `while (io())` and only rewinds at the metering pass
    // (ecSynth.cpp:246), so a second engine would otherwise see an exhausted block
    // and emit no grains at all.
    auto renderBlocks = [&](int blocks) {
      for (int b = 0; b < blocks; ++b) {
        io.zeroOut();
        for (auto &engine : engines) {
          io.frame(0);
          engine->onProcess(io);
        }
      }
    };

    renderBlocks(kWarmupBlocks);  // fill caches, let grains reach steady state

    std::vector<double> walls;
    float peak = 0.0f;
    long long grains = 0;
    for (int r = 0; r < kRepeats; ++r) {
      const auto t0 = std::chrono::steady_clock::now();
      renderBlocks(kBlocks);
      walls.push_back(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

      const float *buf = io.outBuffer(0);
      for (int i = 0; i < kBlockFrames && i < 8; ++i)
        peak = std::max(peak, std::fabs(buf[i]));
    }
    for (auto &engine : engines) grains += engine->grainCounter;

    std::sort(walls.begin(), walls.end());
    const double wall = walls[walls.size() / 2];  // median
    const double audio = secondsFor(kBlocks) * 1000.0;
    const double cpu = wall / audio * 100.0;

    std::printf("%-6d %-10.1f %-10.1f %-9.1f %-12.1f %-10lld %.4f\n",
                n, wall, audio, cpu, cpu / n, grains, peak);
    std::printf("       (construction: %.0f ms for %d engines)\n", buildMs, n);
  }

  std::printf("\nNote: CPU%% is wall/audio*100 on one core; >100%% means slower than real time.\n");
  return 0;
}
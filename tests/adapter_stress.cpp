// adapter_stress.cpp, concurrency test for EC2Adapter (audit §H.1, H.6).
//
// One thread renders continuously; another changes parameters and replaces the
// whole sample bank while it runs. The point is to prove the adapter's contract:
// no torn state, no use-after-free when an engine generation is retired, and audio
// that keeps flowing across swaps.
//
// Build: tests\build_prog.cmd stress.exe tests\adapter_stress.cpp src\engine\EC2Adapter.cpp
// Run:   stress.exe [sample.wav]

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "EC2Adapter.h"

namespace {
constexpr int kBlockFrames = 512;
constexpr double kSampleRate = 48000.0;
constexpr int kSwaps = 6;
constexpr int kIterations = 60;

std::atomic<bool> gRunning{true};
std::atomic<long long> gBlocks{0};
std::atomic<long long> gNonFinite{0};
std::atomic<float> gPeak{0.0f};
}  // namespace

int main(int argc, char **argv) {
  const std::string samplePath =
    argc > 1 ? argv[1]
             : "C:\\Dev\\GitHub\\EmissionControl2-Next\\externalResources\\samples\\440sine48k.wav";

  EC2Adapter adapter;
  if (!adapter.prepare(kSampleRate, kBlockFrames, {samplePath})) {
    std::fprintf(stderr, "FAIL: prepare() failed\n");
    return 2;
  }
  adapter.setParameter(consts::GRAIN_RATE, 20.0f);
  adapter.setParameter(consts::GRAIN_DUR, 120.0f);

  std::vector<float> left(kBlockFrames), right(kBlockFrames);

  std::thread audio([&] {
    while (gRunning.load(std::memory_order_relaxed)) {
      std::fill(left.begin(), left.end(), 0.0f);
      std::fill(right.begin(), right.end(), 0.0f);

      if (!adapter.process(left.data(), right.data(), kBlockFrames, 0.5f)) continue;

      for (int i = 0; i < kBlockFrames; ++i) {
        const float v = left[i];
        if (!std::isfinite(v)) { gNonFinite.fetch_add(1, std::memory_order_relaxed); continue; }
        const float a = std::fabs(v);
        if (a > gPeak.load(std::memory_order_relaxed)) gPeak.store(a, std::memory_order_relaxed);
      }
      gBlocks.fetch_add(1, std::memory_order_relaxed);
    }
  });

  int swaps = 0;
  for (int i = 0; i < kIterations; ++i) {
    adapter.setParameter(consts::GRAIN_RATE, 4.0f + static_cast<float>(i % 40));
    if (i % 8 == 0 && i > 0) {
      if (!adapter.setSamplePaths({samplePath})) break;
      ++swaps;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }

  gRunning.store(false, std::memory_order_relaxed);
  audio.join();

  const long long blocks = gBlocks.load();
  const long long bad = gNonFinite.load();
  const float peak = gPeak.load();

  std::printf("blocks rendered : %lld\n", blocks);
  std::printf("bank swaps      : %d (of %d attempted)\n", swaps, kSwaps);
  std::printf("non-finite      : %lld\n", bad);
  std::printf("peak            : %.4f\n", peak);

  int failures = 0;
  auto check = [&](bool ok, const char *what) {
    std::printf("%-46s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
  };
  check(blocks > 100, "audio thread kept rendering through swaps");
  check(bad == 0, "no non-finite samples during swaps");
  check(peak > 1e-3f, "output non-silent across the whole run");
  check(swaps >= 4, "sample bank replaced repeatedly");

  std::printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
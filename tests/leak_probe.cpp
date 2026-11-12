// leak_probe.cpp -- does the engine grow while it plays?
//
// Runs the facade for several thousand blocks with a note held and reports the
// process working set, so a leak in the grain path shows up as a trend rather
// than as a crash noticed later in a host.

#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "EngineFacade.h"

static size_t workingSetKb()
{
  PROCESS_MEMORY_COUNTERS info{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) return 0;
  return info.WorkingSetSize / 1024;
}

int main(int argc, char **argv)
{
  const std::string sample = argc > 1 ? argv[1]
                                      : "C:\\Dev\\GitHub\\EmissionControl2-Next\\externalResources\\samples\\440sine48k.wav";
  const int block = 512;

  Engine engine;
  if (!engine.prepare(48000.0, block, 6, {sample}))
  {
    std::printf("prepare failed\n");
    return 2;
  }

  engine.setParameter(ec2param::GrainRate, 12.0f);
  engine.setParameter(ec2param::GrainDuration, 120.0f);
  engine.setParameter(ec2param::Amplitude, -6.0f);

  std::vector<float> left(block, 0.0f), right(block, 0.0f);

  engine.noteOn(69, 1.0f);

  const size_t start = workingSetKb();
  std::printf("start %zu KB\n", start);

  const int totalBlocks = 6000; // about 64 seconds at 48 kHz
  float peak = 0.0f;
  for (int b = 1; b <= totalBlocks; ++b)
  {
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    engine.render(left.data(), right.data(), block);
    for (float v : left) peak = (std::max)(peak, std::fabs(v));

    if (b % 600 == 0)
    {
      float l = 0.f, r = 0.f;
      int grains = 0;
      engine.meters(l, r, grains);
      std::printf("block %5d  %6zu KB  +%zu KB  grains %d  peak %.3f\n",
                  b, workingSetKb(), workingSetKb() - start, grains, peak);
    }
  }

  const size_t end = workingSetKb();
  std::printf("end %zu KB, growth %zu KB over %d blocks\n", end, end - start, totalBlocks);
  return 0;
}

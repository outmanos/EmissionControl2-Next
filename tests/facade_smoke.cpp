// facade_smoke.cpp — does the Engine facade produce audio for a note?
//
// Isolates the plugin's audio path from host MIDI routing: if this is silent,
// the problem is in the engine or the sample; if it is loud, the problem is in
// the plugin or the host.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "EngineFacade.h"

int main(int argc, char **argv)
{
  const std::string sample = argc > 1 ? argv[1]
                                      : "C:\\Dev\\GitHub\\EmissionControl2-Next\\externalResources\\samples\\440sine48k.wav";

  Engine engine;
  const int block = 512;
  const bool ok = engine.prepare(48000.0, block, 6, {sample});
  std::printf("prepare        : %s\n", ok ? "ok" : "FAILED");
  if (!ok) return 2;

  engine.setParameterRange(ec2param::GrainRate, 0.5f, 20.0f);
  engine.setModRange(ec2param::GrainRate, 0.0f, 0.75f);
  engine.setLfoFrequencyRange(0, 0.01f, 12.0f);
  engine.setParameter(ec2param::GrainRate, 8.0f);
  engine.setParameter(ec2param::GrainDuration, 120.0f);
  engine.setParameter(ec2param::Amplitude, -6.0f);

  engine.noteOn(69, 1.0f, false);
  const bool freeRunPreservesHost =
    engine.parameter(ec2param::GrainRate) == 8.0f &&
    engine.parameter(ec2param::Playback) == 1.0f &&
    engine.parameter(ec2param::Amplitude) == -6.0f;
  engine.noteOff(69);

  engine.noteOn(81, 1.0f, true);
  const bool midiMapsIndependently =
    engine.parameter(ec2param::GrainRate) == 4.0f &&
    engine.parameter(ec2param::Playback) == 2.0f;
  engine.setParameter(ec2param::GrainRate, 12.0f);
  engine.setParameter(ec2param::Playback, 0.5f);
  const bool midiSurvivesHostEdits =
    engine.parameter(ec2param::GrainRate) == 6.0f &&
    engine.parameter(ec2param::Playback) == 2.0f;
  engine.allNotesOff();

  std::printf("free-run host values preserved: %s\n", freeRunPreservesHost ? "ok" : "FAILED");
  std::printf("MIDI pitch/grain independence : %s\n", midiMapsIndependently ? "ok" : "FAILED");
  std::printf("MIDI values after host edits  : %s\n", midiSurvivesHostEdits ? "ok" : "FAILED");

  std::vector<float> left(block, 0.0f), right(block, 0.0f);
  float peakBefore = 0.0f;
  for (int b = 0; b < 20; ++b)
  {
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    engine.render(left.data(), right.data(), block);
    for (float v : left) peakBefore = std::max(peakBefore, std::fabs(v));
  }

  engine.noteOn(69, 1.0f, false);
  float peakAfter = 0.0f;
  for (int b = 0; b < 40; ++b)
  {
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    engine.render(left.data(), right.data(), block);
    for (float v : left) peakAfter = std::max(peakAfter, std::fabs(v));
  }

  const float scan = engine.scanPosition();
  std::printf("peak before note: %.6f\n", peakBefore);
  std::printf("peak after note : %.6f\n", peakAfter);
  std::printf("scan position   : %.6f\n", scan);

  const bool scanOk = std::isfinite(scan) && scan >= 0.0f && scan <= 1.0f;
  const bool pass = freeRunPreservesHost && midiMapsIndependently && midiSurvivesHostEdits &&
                    peakBefore < 1e-6f && peakAfter > 1e-3f && scanOk;
  std::printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

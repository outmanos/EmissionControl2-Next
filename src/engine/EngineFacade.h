#pragma once

// Standard-library-only facade over the EC2 instrument layer.
//
// The plugin layer must never include AlloLib headers: iPlug2 compiles its own
// IGraphics sources inside the plugin target, and AlloLib's glad headers collide
// with iPlug2's GL2 loader. Everything engine-shaped stays behind this interface.

#include <memory>
#include <string>
#include <vector>

/// EC2 parameter indices, mirrored here so this header stays free of AlloLib.
/// EngineFacade.cpp static_asserts that they still match consts::paramNames.
namespace ec2param {
enum
{
  GrainRate = 0,
  Async = 1,
  Interm = 2,
  Streams = 3,
  Playback = 4,
  FilterCenter = 5,
  Resonance = 6,
  SoundFile = 7,
  ScanBegin = 8,
  ScanRange = 9,
  ScanSpeed = 10,
  GrainDuration = 11,
  Envelope = 12,
  Pan = 13,
  Amplitude = 14
};
} // namespace ec2param

class Engine {
 public:
  Engine();
  ~Engine();

  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;

  /// Allocates all voices. Call off the audio thread.
  bool prepare(double sampleRate, int blockSize, int voices, const std::vector<std::string> &samples);

  bool isPrepared() const;
  int blockSize() const;

  /// Parameter index is the EC2 parameter enum value (see consts::paramNames).
  void setParameter(int paramIndex, float value);
  void setParameterRange(int paramIndex, float min, float max);

  /// Modulation depth 0..1 for a parameter.
  void setModDepth(int paramIndex, float value);
  void setModRange(int paramIndex, float min, float max);
  /// Which LFO modulates a parameter, 0..5.
  void setModLfo(int paramIndex, int lfoIndex);
  /// LFO field: 0 shape, 1 polarity, 2 frequency, 3 duty.
  void setLfo(int lfoIndex, int field, float value);
  void setLfoFrequencyRange(int lfoIndex, float min, float max);

  float scanPosition() const;

  void noteOn(int midiNote, float velocity, bool pitchFromNote = true);
  float parameter(int paramIndex) const;
  void noteOff(int midiNote);
  void allNotesOff();

  /// Adds the mixed voices into outL/outR. Audio thread only.
  void render(float *outL, float *outR, int frames);

  /// Replaces the sample on every voice. Call from the message thread.
  bool loadSample(const std::string &path);
  bool setSamples(const std::vector<std::string> &paths);

  /// Active grain source sample-frame indices for display. Returns the count.
  int grains(float *out, int max) const;

  /// Output metering for display: peak per channel and active grain count.
  void meters(float &peakL, float &peakR, int &activeGrains) const;

  /// Read-only view of the loaded sample for display. Returns nullptr if none.
  /// Frames and channels are written to the out parameters.
  const float *waveform(int &frames, int &channels) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m;
};

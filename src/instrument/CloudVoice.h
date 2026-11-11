// CloudVoice.h, one MIDI note owns one EC2 cloud, gated by a gain envelope.
//
// Validated by tests/cloud_voice_proto.cpp (audit §G.9): silent until note-on,
// exact 12-TET via PLAYBACK, release to silence, idle voices cost nothing.

#ifndef CLOUD_VOICE_H
#define CLOUD_VOICE_H

#include <cmath>
#include <string>
#include <vector>

#include "EC2Adapter.h"

class CloudVoice {
 public:
  enum class State { Idle, Active, Releasing };

  bool prepare(double sampleRate, int blockSize, const std::vector<std::string> &samplePaths) {
    mSampleRate = sampleRate;
    mBlockSize = blockSize;
    return mAdapter.prepare(sampleRate, blockSize, samplePaths);
  }

  /// 12-TET against the sample's natural pitch (A4 = 69 = 440 Hz for the bundled
  /// sample). A microtonal system replaces this one function.
  static float playbackRateForNote(int midiNote) {
    return std::pow(2.0f, (midiNote - 69) / 12.0f);
  }

  void noteOn(int midiNote, float velocity, unsigned long long stamp) {
    mNote = midiNote;
    mStamp = stamp;
    mAdapter.setParameter(consts::PLAYBACK, playbackRateForNote(midiNote));
    mAdapter.setParameter(consts::AMPLITUDE, -24.0f + 18.0f * velocity);
    mTargetGain = 1.0f;
    mState = State::Active;
  }

  void noteOff() {
    mTargetGain = 0.0f;
    mState = State::Releasing;
  }

  void render(float *outL, float *outR, int frames) {
    if (mState == State::Idle) return;

    mAdapter.process(outL, outR, frames, mGain);

    // One-pole gain approach: ~10 ms attack, ~60 ms release, smooth per block.
    const double tau = (mState == State::Releasing) ? 0.060 : 0.010;
    const double k = frames / (frames + mSampleRate * tau);
    mGain += (mTargetGain - mGain) * static_cast<float>(k);

    if (mState == State::Releasing && mGain < 1e-3f) {
      mGain = 0.0f;
      mState = State::Idle;
    }
  }

  bool busy() const { return mState != State::Idle; }
  bool releasing() const { return mState == State::Releasing; }
  int note() const { return mNote; }
  unsigned long long stamp() const { return mStamp; }
  EC2Adapter &adapter() { return mAdapter; }

 private:
  EC2Adapter mAdapter;
  double mSampleRate = 48000.0;
  int mBlockSize = 0;
  State mState = State::Idle;
  int mNote = -1;
  unsigned long long mStamp = 0;
  float mGain = 0.0f;
  float mTargetGain = 0.0f;
};

#endif  // CLOUD_VOICE_H
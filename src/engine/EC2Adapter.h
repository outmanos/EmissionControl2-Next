// EC2Adapter.h, the plugin-facing boundary around one EC2 engine instance.
//
// Purpose: give the plugin/instrument layer a surface that (a) owns no AlloLib type
// in its interface, (b) never lets a message thread touch audio-thread state, and
// (c) makes sample-bank replacement safe. See audit §G.4 and §H.
//
// Threading contract
//   prepare()/setSamplePaths()/setParameter()   message thread
//   process()                                   audio thread ONLY
//   destructor                                  only when audio is stopped
//
// Fixes relative to driving ecSynth directly:
//   H.1  sample bank replaced by building a new engine and swapping a pointer,
//        never by mutating the live engine's soundClip (see publish()).
//   H.6  parameters are plain atomics; the audio thread applies them at block
//        start, so al::Parameter is only ever written from the audio thread.

#ifndef EC2_ADAPTER_H
#define EC2_ADAPTER_H

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ecSynth.h"

class EC2Adapter {
 public:
  EC2Adapter();
  ~EC2Adapter();

  EC2Adapter(const EC2Adapter &) = delete;
  EC2Adapter &operator=(const EC2Adapter &) = delete;

  /// Message thread, audio stopped. Builds the first engine and publishes it.
  bool prepare(double sampleRate, int blockSize, const std::vector<std::string> &samplePaths);

  /// Message thread, safe while audio runs. Builds a replacement engine off the
  /// audio thread and hands it over; the previous one is retired and freed.
  bool setSamplePaths(const std::vector<std::string> &samplePaths);

  /// Message thread, any time. Values are picked up by the audio thread at the
  /// start of the next block.
  void setParameter(int paramIndex, float value);
  float parameter(int paramIndex) const;
  void setParameterRange(int paramIndex, float min, float max);

  /// Modulation depth (0..1) for a parameter, applied at block start.
  void setModDepth(int paramIndex, float value);
  void setModRange(int paramIndex, float min, float max);
  /// Which LFO (0..5) modulates a parameter.
  void setModLfo(int paramIndex, int lfoIndex);
  /// LFO controls: field 0 shape, 1 polarity, 2 frequency, 3 duty.
  void setLfo(int lfoIndex, int field, float value);
  void setLfoFrequencyRange(int lfoIndex, float min, float max);

  float scanPosition() const { return mScanPos.load(std::memory_order_acquire); }

  /// Audio thread. Adds the engine's output into outL/outR (never overwrites).
  /// Returns false if the adapter is not ready for audio.
  bool process(float *outL, float *outR, int frames, float gain);

  bool isPrepared() const { return mPrepared.load(std::memory_order_acquire); }

  /// Active grain source indices, published by the audio thread for display.
  /// Safe because the audio thread is the only writer and it owns the voice list.
  int grainSnapshot(float *out, int max) const {
    const int n = std::min(mGrainCount.load(std::memory_order_acquire), max);
    for (int i = 0; i < n; ++i) out[i] = mGrainIdx[i].load(std::memory_order_relaxed);
    return n;
  }

  /// Output metering, written by the audio thread. Best effort for display.
  void meters(float &peakL, float &peakR, int &activeGrains) const {
    peakL = mPeakL.load(std::memory_order_relaxed);
    peakR = mPeakR.load(std::memory_order_relaxed);
    // Published by the audio thread. Reading the engine here raced with grain
    // creation and was a plausible cause of the crash after a few minutes.
    activeGrains = mGrainCount.load(std::memory_order_relaxed);
  }

  /// First loaded sample clip, for display only. Null until a sample is loaded.
  const util::buffer<float> *firstSample() const {
    if (!mEngine || mEngine->soundClip.empty()) return nullptr;
    return mEngine->soundClip.front().get();
  }
  int blockSize() const { return mBlockSize; }
  const std::vector<std::string> &samplePaths() const { return mPaths; }

 private:
  std::unique_ptr<ecSynth> buildEngine(const std::vector<std::string> &samplePaths);
  void publish(std::unique_ptr<ecSynth> next);
  void waitForBlockTicks(int ticks);

  std::unique_ptr<ecSynth> mEngine;                  // message-thread owner
  std::vector<std::unique_ptr<ecSynth>> mRetired;    // previous generations, bounded
  std::atomic<ecSynth *> mLive{nullptr};             // what the audio thread reads
  std::atomic<uint64_t> mBlockTick{0};               // audio thread increments per block
  std::atomic<bool> mAudioRunning{false};
  std::atomic<bool> mPrepared{false};

  al::AudioIOData mIO;
  double mSampleRate = 48000.0;
  int mBlockSize = 0;
  std::vector<std::string> mPaths;
  std::array<std::atomic<float>, consts::NUM_PARAMS> mParams;
  std::array<std::atomic<float>, consts::NUM_PARAMS> mParamMin;
  std::array<std::atomic<float>, consts::NUM_PARAMS> mParamMax;
  std::array<std::atomic<float>, consts::NUM_PARAMS> mModDepths;
  std::array<std::atomic<float>, consts::NUM_PARAMS> mModMin;
  std::array<std::atomic<float>, consts::NUM_PARAMS> mModMax;
  std::array<std::atomic<int>, consts::NUM_PARAMS> mModLfo;
  std::array<std::atomic<float>, consts::NUM_LFOS * 4> mLfo;
  std::array<std::atomic<float>, consts::NUM_LFOS> mLfoMin;
  std::array<std::atomic<float>, consts::NUM_LFOS> mLfoMax;
  std::atomic<float> mScanPos{0.0f};
  static constexpr int kMaxGrainDots = 128;
  std::array<std::atomic<float>, kMaxGrainDots> mGrainIdx;
  std::atomic<int> mGrainCount{0};
  std::atomic<float> mPeakL{0.0f};
  std::atomic<float> mPeakR{0.0f};
  // Held while an engine is rebuilt so the audio thread can skip the block
  // instead of touching a half-initialized IO object.
  std::mutex mSwapMutex;
};

#endif  // EC2_ADAPTER_H
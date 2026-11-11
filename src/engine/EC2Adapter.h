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

#include <array>
#include <atomic>
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

  /// Audio thread. Adds the engine's output into outL/outR (never overwrites).
  /// Returns false if the adapter is not ready for audio.
  bool process(float *outL, float *outR, int frames, float gain);

  bool isPrepared() const { return mPrepared.load(std::memory_order_acquire); }
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
};

#endif  // EC2_ADAPTER_H
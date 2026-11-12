// VoiceManager.h, fixed-size cloud pool with oldest-note stealing.
//
// Voices are pre-allocated at prepare time: engine construction costs ~13 ms each
// (audit §G.8), which is fine in prepareToPlay and impossible on a note-on.

#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include <algorithm>
#include <memory>
#include <vector>

#include "CloudVoice.h"

class VoiceManager {
 public:
  bool prepare(double sampleRate, int blockSize, int voiceCount,
               const std::vector<std::string> &samplePaths) {
    if (voiceCount <= 0) return false;
    mVoices.clear();
    mVoices.reserve(static_cast<size_t>(voiceCount));
    for (int i = 0; i < voiceCount; ++i) {
      std::unique_ptr<CloudVoice> voice(new CloudVoice());
      if (!voice->prepare(sampleRate, blockSize, samplePaths)) return false;
      mVoices.push_back(std::move(voice));
    }
    return true;
  }

  int voiceCount() const { return static_cast<int>(mVoices.size()); }

  /// Returns the note actually allocated, or -1 if no voice is available.
  int noteOn(int midiNote, float velocity, bool pitchFromNote = true) {
    CloudVoice *target = nullptr;
    for (auto &voice : mVoices)
      if (!voice->busy() || voice->releasing()) { target = voice.get(); break; }

    if (!target) {  // steal the oldest held note
      unsigned long long oldest = ~0ull;
      for (auto &voice : mVoices)
        if (voice->stamp() < oldest) { oldest = voice->stamp(); target = voice.get(); }
      ++mSteals;
    }
    if (!target) return -1;

    target->noteOn(midiNote, velocity, ++mCounter, pitchFromNote);
    if (pitchFromNote) {
      target->adapter().setParameter(consts::GRAIN_RATE, mGrainRate * kMidiGrainRateScale);
    } else {
      target->adapter().setParameter(consts::GRAIN_RATE, mGrainRate);
      target->adapter().setParameter(consts::PLAYBACK, mPlaybackRate);
      target->adapter().setParameter(consts::AMPLITUDE, mAmplitude);
    }
    ++mNoteOns;
    return midiNote;
  }

  void noteOff(int midiNote) {
    for (auto &voice : mVoices)
      if (voice->busy() && !voice->releasing() && voice->note() == midiNote) voice->noteOff();
    ++mNoteOffs;
  }

  void allNotesOff() {
    for (auto &voice : mVoices)
      if (voice->busy() && !voice->releasing()) voice->noteOff();
  }

  void render(float *outL, float *outR, int frames) {
    // Voice sum is unnormalized: a master gain policy belongs to the host layer.
    for (auto &voice : mVoices) voice->render(outL, outR, frames);
  }

  void setModDepth(int paramIndex, float value) {
    for (auto &voice : mVoices) voice->adapter().setModDepth(paramIndex, value);
  }

  void setParameterRange(int paramIndex, float min, float max) {
    for (auto &voice : mVoices) voice->adapter().setParameterRange(paramIndex, min, max);
  }

  void setModRange(int paramIndex, float min, float max) {
    for (auto &voice : mVoices) voice->adapter().setModRange(paramIndex, min, max);
  }

  void setLfoFrequencyRange(int lfoIndex, float min, float max) {
    for (auto &voice : mVoices) voice->adapter().setLfoFrequencyRange(lfoIndex, min, max);
  }

  float scanPosition() const {
    if (mVoices.empty()) return 0.0f;
    return mVoices.front()->adapter().scanPosition();
  }

  void setModLfo(int paramIndex, int lfoIndex) {
    for (auto &voice : mVoices) voice->adapter().setModLfo(paramIndex, lfoIndex);
  }

  void setLfo(int lfoIndex, int field, float value) {
    for (auto &voice : mVoices) voice->adapter().setLfo(lfoIndex, field, value);
  }

  /// Replaces the sample on every voice. Message thread.
  bool loadSample(const std::string &path) { return loadSample(std::vector<std::string>{path}); }

  bool loadSample(const std::vector<std::string> &paths) {
    bool ok = true;
    for (auto &voice : mVoices) ok = voice->adapter().setSamplePaths(paths) && ok;
    return ok;
  }

  /// Grain positions from the first voice, for display.
  int grains(float *out, int max) const {
    if (mVoices.empty()) return 0;
    return mVoices.front()->adapter().grainSnapshot(out, max);
  }

  /// Aggregate metering across voices.
  void meters(float &peakL, float &peakR, int &activeGrains) const {
    peakL = peakR = 0.0f;
    activeGrains = 0;
    for (const auto &voice : mVoices) {
      float l = 0.0f, r = 0.0f;
      int grains = 0;
      voice->adapter().meters(l, r, grains);
      peakL = std::max(peakL, l);
      peakR = std::max(peakR, r);
      activeGrains += grains;
    }
  }

  /// Read-only access to the first voice's loaded sample, for display.
  const util::buffer<float> *firstSample() const {
    if (mVoices.empty()) return nullptr;
    return mVoices.front()->adapter().firstSample();
  }

  float parameter(int paramIndex) const {
    if (mVoices.empty()) return 0.0f;
    return mVoices.front()->adapter().parameter(paramIndex);
  }

  /// Applies a parameter to every voice. Called from the message thread; each
  /// adapter stores it atomically and the audio thread applies it per block.
  void setParameter(int paramIndex, float value) {
    if (paramIndex == consts::GRAIN_RATE) mGrainRate = value;
    else if (paramIndex == consts::PLAYBACK) mPlaybackRate = value;
    else if (paramIndex == consts::AMPLITUDE) mAmplitude = value;

    for (auto &voice : mVoices) {
      const bool noteDriven = voice->busy() && voice->pitchFromNote();
      if (noteDriven && paramIndex == consts::GRAIN_RATE) {
        voice->adapter().setParameter(paramIndex, value * kMidiGrainRateScale);
      } else if (noteDriven && (paramIndex == consts::PLAYBACK || paramIndex == consts::AMPLITUDE)) {
        continue;
      } else {
        voice->adapter().setParameter(paramIndex, value);
      }
    }
  }

  int activeVoices() const {
    int n = 0;
    for (const auto &voice : mVoices) n += voice->busy() ? 1 : 0;
    return n;
  }
  int noteOns() const { return mNoteOns; }
  int noteOffs() const { return mNoteOffs; }
  int steals() const { return mSteals; }

 private:
  static constexpr float kMidiGrainRateScale = 0.5f;
  std::vector<std::unique_ptr<CloudVoice>> mVoices;
  float mGrainRate = 1.0f;
  float mPlaybackRate = 1.0f;
  float mAmplitude = -6.0f;
  int mNoteOns = 0, mNoteOffs = 0, mSteals = 0;
  unsigned long long mCounter = 0;
};

#endif  // VOICE_MANAGER_H

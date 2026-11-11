// VoiceManager.h, fixed-size cloud pool with oldest-note stealing.
//
// Voices are pre-allocated at prepare time: engine construction costs ~13 ms each
// (audit §G.8), which is fine in prepareToPlay and impossible on a note-on.

#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

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
  int noteOn(int midiNote, float velocity) {
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

    target->noteOn(midiNote, velocity, ++mCounter);
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

  int activeVoices() const {
    int n = 0;
    for (const auto &voice : mVoices) n += voice->busy() ? 1 : 0;
    return n;
  }
  int noteOns() const { return mNoteOns; }
  int noteOffs() const { return mNoteOffs; }
  int steals() const { return mSteals; }

 private:
  std::vector<std::unique_ptr<CloudVoice>> mVoices;
  int mNoteOns = 0, mNoteOffs = 0, mSteals = 0;
  unsigned long long mCounter = 0;
};

#endif  // VOICE_MANAGER_H
// cloud_voice_proto.cpp, note-gated granular voice prototype (audit §F.6.1).
//
// The last open design question: can one MIDI note own one whole ecSynth cloud?
// A naked ecSynth emits grains forever, so a voice needs (a) gating, (b) a pitch
// mapping, (c) an allocation policy. This prototype proves all three, and measures
// the pitch mapping rather than assuming it.
//
// Each voice renders into its own scratch AudioIOData and is summed into the master
// with a gain ramp. That shape is deliberate: it is what a plugin adapter needs
// anyway, and it sidesteps the shared-buffer frame trap documented in audit §F.3.
//
// Build: tests\build_smoke.cmd cloud_voice_proto.cpp proto.exe
// Run:   proto.exe [sample.wav]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ecSynth.h"
#include "Gamma/Gamma.h"

namespace {

constexpr int kBlockFrames = 512;
constexpr double kSampleRate = 48000.0;
constexpr int kMaxVoices = 4;

// --- A voice: one ecSynth plus its own output buffer and a gain envelope --------
class CloudVoice {
 public:
  void prepare(const std::string &samplePath) {
    mScratch.channelsIn(0);
    mScratch.channelsOut(2);
    mScratch.framesPerBuffer(kBlockFrames);
    mScratch.framesPerSecond(kSampleRate);

    mEngine.setOutChannels(0, 2);  // before initialize(): audit §C.5.2
    mEngine.initialize(&mScratch);
    if (!mEngine.loadSoundFileOffline(samplePath)) mLoaded = false;

    // The default 1 Hz grain rate is far too sparse for a playable cloud.
    // 8 Hz with 120 ms grains keeps overlap to ~1 grain at a time, so the pitch
    // test measures the playback mapping rather than grain-interference artifacts.
    mEngine.ECParameters[consts::GRAIN_RATE]->setParam(8.0f);
    mEngine.ECParameters[consts::GRAIN_DUR]->setParam(120.0f);

    // PLAYBACK is clamped by its *current* range (-2..2 by default); widen it so a
    // two-octave keyboard does not silently clamp. Audit §F.5.
    mEngine.ECParameters[consts::PLAYBACK]->setCurrentMin(-4.0f);
    mEngine.ECParameters[consts::PLAYBACK]->setCurrentMax(4.0f);
  }

  bool loaded() const { return mLoaded; }

  // Reference pitch: the bundled sample is a 440 Hz sine, i.e. A4 = note 69.
  // 12-TET playback ratio; a microtonal system only has to replace this function.
  static float playbackRateForNote(int midiNote) {
    return std::pow(2.0f, (midiNote - 69) / 12.0f);
  }

  void noteOn(int midiNote, float velocity, unsigned long long stamp) {
    mNote = midiNote;
    mStamp = stamp;
    mEngine.ECParameters[consts::PLAYBACK]->setParam(playbackRateForNote(midiNote));
    mEngine.ECParameters[consts::AMPLITUDE]->setParam(-24.0f + 18.0f * velocity);
    mTargetGain = 1.0f;
    mState = State::Active;
  }

  void noteOff() {
    mTargetGain = 0.0f;
    mState = State::Releasing;
  }

  bool busy() const { return mState != State::Idle; }
  bool releasing() const { return mState == State::Releasing; }
  int note() const { return mNote; }
  unsigned long long stamp() const { return mStamp; }

  // Render into the master buffers (interleaved-by-channel float arrays).
  void render(float *masterL, float *masterR) {
    if (mState == State::Idle) return;

    mScratch.zeroOut();
    mScratch.frame(0);
    mEngine.onProcess(mScratch);

    const float *l = mScratch.outBuffer(0);
    const float *r = mScratch.outBuffer(1);
    for (int i = 0; i < kBlockFrames; ++i) {
      // Linear ramp toward the target; ~10 ms attack, ~60 ms release.
      const float step = (mTargetGain - mGain) * 0.02f;
      mGain += step;
      masterL[i] += l[i] * mGain;
      masterR[i] += r[i] * mGain;
    }

    if (mState == State::Releasing && mGain < 1e-3f) {
      mGain = 0.0f;
      mState = State::Idle;
    }
  }

  void hardStop() {
    mState = State::Idle;
    mGain = 0.0f;
    mTargetGain = 0.0f;
  }

 private:
  enum class State { Idle, Active, Releasing };
  ecSynth mEngine;
  al::AudioIOData mScratch;
  bool mLoaded = true;
  State mState = State::Idle;
  int mNote = -1;
  unsigned long long mStamp = 0;
  float mGain = 0.0f;
  float mTargetGain = 0.0f;
};

// --- A voice pool with oldest-note stealing -----------------------------------
class CloudSynth {
 public:
  void prepare(const std::string &samplePath) {
    mVoice.resize(kMaxVoices);
    for (int i = 0; i < kMaxVoices; ++i) {
      mVoice[i].reset(new CloudVoice());
      mVoice[i]->prepare(samplePath);
      mLoaded = mLoaded && mVoice[i]->loaded();
    }
  }
  bool loaded() const { return mLoaded; }

  int noteOn(int midiNote, float velocity) {
    CloudVoice *free = nullptr;
    for (int i = 0; i < kMaxVoices; ++i)
      if (!mVoice[i]->busy() || mVoice[i]->releasing()) { free = mVoice[i].get(); break; }

    if (!free) {  // steal the oldest held note
      unsigned long long oldest = ~0ull;
      for (int i = 0; i < kMaxVoices; ++i)
        if (mVoice[i]->stamp() < oldest) { oldest = mVoice[i]->stamp(); free = mVoice[i].get(); }
      ++mStolen;
    }
    free->noteOn(midiNote, velocity, ++mCounter);
    ++mNoteOns;
    return free->note();
  }

  void noteOff(int midiNote) {
    for (int i = 0; i < kMaxVoices; ++i)
      if (mVoice[i]->busy() && !mVoice[i]->releasing() && mVoice[i]->note() == midiNote)
        mVoice[i]->noteOff();
    ++mNoteOffs;
  }

  void render(float *l, float *r) {
    for (int i = 0; i < kMaxVoices; ++i) mVoice[i]->render(l, r);
  }

  int activeVoices() const {
    int n = 0;
    for (int i = 0; i < kMaxVoices; ++i) n += mVoice[i]->busy() ? 1 : 0;
    return n;
  }
  int noteOns() const { return mNoteOns; }
  int noteOffs() const { return mNoteOffs; }
  int stolen() const { return mStolen; }

 private:
  std::vector<std::unique_ptr<CloudVoice>> mVoice;
  bool mLoaded = true;
  int mNoteOns = 0, mNoteOffs = 0, mStolen = 0;
  unsigned long long mCounter = 0;
};

// Pitch by Goertzel scan over a candidate range. Autocorrelation is unreliable here:
// every grain starts mid-waveform, so the sum is amplitude-modulated at the grain
// rate, and lag quantisation is too coarse to distinguish 840 from 880 Hz.
double goertzelPower(const std::vector<float> &x, int start, int n, double hz) {
  const double w = 2.0 * M_PI * hz / kSampleRate;
  const double coeff = 2.0 * std::cos(w);
  double s0 = 0.0, s1 = 0.0, s2 = 0.0;
  for (int i = 0; i < n; ++i) {
    s0 = x[start + i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

double estimateHz(const std::vector<float> &x) {
  const int n = static_cast<int>(x.size());
  const int start = 4000;
  const int window = std::min(n - start, 16384);
  if (window < 4096) return 0.0;

  double low = 100.0, high = 2000.0, bestHz = 0.0;
  for (int pass = 0; pass < 3; ++pass) {
    const double step = (high - low) / 200.0;
    double best = -1.0;
    for (double hz = low; hz <= high; hz += step) {
      const double p = goertzelPower(x, start, window, hz);
      if (p > best) { best = p; bestHz = hz; }
    }
    low = std::max(100.0, bestHz - step);
    high = bestHz + step;
  }
  return bestHz;
}

struct Render {
  std::vector<float> left, right;
  float peak = 0.0f;
};

// Run a scripted gesture: calls edit() before each block, renders, then returns audio.
template <class Fn>
Render play(CloudSynth &synth, int blocks, Fn edit) {
  Render out;
  out.left.assign(static_cast<size_t>(blocks) * kBlockFrames, 0.0f);
  out.right.assign(static_cast<size_t>(blocks) * kBlockFrames, 0.0f);
  for (int b = 0; b < blocks; ++b) {
    edit(b);
    float *l = &out.left[static_cast<size_t>(b) * kBlockFrames];
    float *r = &out.right[static_cast<size_t>(b) * kBlockFrames];
    synth.render(l, r);
    for (int i = 0; i < kBlockFrames; ++i) out.peak = std::max(out.peak, std::fabs(l[i]));
  }
  return out;
}

int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok) ++failures;
}

}  // namespace

int main(int argc, char **argv) {
  const std::string samplePath =
    argc > 1 ? argv[1]
             : "C:\\Dev\\GitHub\\EmissionControl2-Next\\externalResources\\samples\\440sine48k.wav";

  CloudSynth synth;
  synth.prepare(samplePath);
  if (!synth.loaded()) {
    std::fprintf(stderr, "FAIL: could not load '%s'\n", samplePath.c_str());
    return 2;
  }

  // 1, silence before any note -------------------------------------------------
  {
    const Render r = play(synth, 20, [](int) {});
    check(r.peak < 1e-6f, "silent before any note-on");
  }

  // 2, pitch mapping: A4 renders ~440 Hz, A5 ~880 Hz ---------------------------
  double hzA4 = 0.0, hzA5 = 0.0;
  {
    Render r = play(synth, 40, [&](int b) { if (b == 2) synth.noteOn(69, 1.0f); });
    check(r.peak > 1e-3f, "audible after note-on (A4)");
    hzA4 = estimateHz(r.left);

    synth.noteOff(69);
    play(synth, 20, [](int) {});  // let the release finish
    check(synth.activeVoices() == 0, "voice returns to idle after release");

    Render r2 = play(synth, 40, [&](int b) { if (b == 2) synth.noteOn(81, 1.0f); });
    hzA5 = estimateHz(r2.left);
    synth.noteOff(81);
    play(synth, 20, [](int) {});
  }
  std::printf("  measured A4 = %.1f Hz (expect ~440), A5 = %.1f Hz (expect ~880)\n", hzA4, hzA5);
  check(std::fabs(hzA4 - 440.0) < 30.0, "A4 pitch within 30 Hz of 440");
  check(std::fabs(hzA5 - 880.0) < 60.0, "A5 pitch within 60 Hz of 880");
  check(std::fabs(hzA5 / std::max(hzA4, 1.0) - 2.0) < 0.08, "octave ratio ~2.0");

  // 3, release actually silences ----------------------------------------------
  {
    synth.noteOn(60, 1.0f);
    play(synth, 20, [](int) {});
    synth.noteOff(60);
    play(synth, 20, [](int) {});  // release ramp
    const Render silent = play(synth, 10, [](int) {});
    check(silent.peak < 1e-6f, "silent after release completes");
  }

  // 4, polyphony and stealing --------------------------------------------------
  {
    synth.noteOn(60, 1.0f);
    synth.noteOn(64, 1.0f);
    synth.noteOn(67, 1.0f);
    synth.noteOn(71, 1.0f);
    check(synth.activeVoices() == 4, "four held notes fill four voices");

    const int before = synth.stolen();
    synth.noteOn(74, 1.0f);  // fifth note must steal
    check(synth.stolen() == before + 1, "fifth note steals the oldest voice");

    const Render chord = play(synth, 30, [](int) {});
    check(chord.peak > 1e-3f, "chord of stolen+held voices still renders");

    for (int n : {60, 64, 67, 71, 74}) synth.noteOff(n);
    play(synth, 40, [](int) {});
    check(synth.activeVoices() == 0, "all voices idle after all note-offs");
  }

  std::printf("\nnote-ons %d, note-offs %d, steals %d, failures %d\n",
              synth.noteOns(), synth.noteOffs(), synth.stolen(), failures);
  std::printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
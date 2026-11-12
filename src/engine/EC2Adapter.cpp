// EC2Adapter.cpp, see EC2Adapter.h for the threading contract.

#include "EC2Adapter.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace {
// How long setSamplePaths() will wait for the audio thread to clear the previous
// engine before it stops waiting and simply retains it. Bounded so a stopped or
// stalled audio thread can never hang the message thread.
constexpr int kMaxWaitMs = 100;
// Generations kept alive at once: the live one plus two predecessors. Retiring
// more slowly than that risks freeing an engine that a block is still using.
constexpr size_t kMaxRetired = 2;
}  // namespace

EC2Adapter::EC2Adapter() {
  for (auto &p : mParams) p.store(0.0f, std::memory_order_relaxed);
  for (auto &p : mParamMin) p.store(0.0f, std::memory_order_relaxed);
  for (auto &p : mParamMax) p.store(1.0f, std::memory_order_relaxed);
  for (auto &p : mModDepths) p.store(0.0f, std::memory_order_relaxed);
  for (auto &p : mModMin) p.store(0.0f, std::memory_order_relaxed);
  for (auto &p : mModMax) p.store(1.0f, std::memory_order_relaxed);
  for (auto &p : mModLfo) p.store(0, std::memory_order_relaxed);
  for (auto &p : mLfo) p.store(0.0f, std::memory_order_relaxed);
  for (auto &p : mLfoMin) p.store(0.01f, std::memory_order_relaxed);
  for (auto &p : mLfoMax) p.store(30.0f, std::memory_order_relaxed);
  for (auto &p : mGrainIdx) p.store(0.0f, std::memory_order_relaxed);
}

EC2Adapter::~EC2Adapter() {
  // Caller guarantees the audio thread is no longer calling process().
  mLive.store(nullptr, std::memory_order_release);
  mRetired.clear();
  mEngine.reset();
}

std::unique_ptr<ecSynth> EC2Adapter::buildEngine(const std::vector<std::string> &samplePaths) {
  std::unique_ptr<ecSynth> engine(new ecSynth());

  // Order matters: AudioChanIndex is uninitialized until setOutChannels().
  engine->setOutChannels(0, 2);
  engine->initialize(&mIO);

  // Widened so a keyboard beyond +/-2 octaves does not silently clamp.
  engine->ECParameters[consts::PLAYBACK]->setCurrentMin(-4.0f);
  engine->ECParameters[consts::PLAYBACK]->setCurrentMax(4.0f);

  for (const auto &path : samplePaths) engine->loadSoundFileOffline(path);
  return engine;
}

bool EC2Adapter::prepare(double sampleRate, int blockSize,
                         const std::vector<std::string> &samplePaths) {
  if (blockSize <= 0) return false;

  mSampleRate = sampleRate;
  mBlockSize = blockSize;

  mIO.channelsIn(0);
  mIO.channelsOut(2);
  mIO.framesPerBuffer(static_cast<unsigned int>(blockSize));
  mIO.framesPerSecond(sampleRate);
  gam::sampleRate(sampleRate);  // Gamma global; mirrors ecInterface.cpp:144

  mPaths = samplePaths;
  std::unique_ptr<ecSynth> engine = buildEngine(mPaths);
  if (!engine) return false;

  // Mirror the engine's defaults into the atomics so an untouched parameter
  // reproduces the app's startup sound rather than a zero.
  for (int i = 0; i < consts::NUM_PARAMS; ++i) {
    mParams[i].store(engine->ECParameters[i]->getParam(), std::memory_order_relaxed);
    mParamMin[i].store(engine->ECParameters[i]->getCurrentMin(), std::memory_order_relaxed);
    mParamMax[i].store(engine->ECParameters[i]->getCurrentMax(), std::memory_order_relaxed);
    mModMin[i].store(engine->ECModParameters[i]->param.getCurrentMin(), std::memory_order_relaxed);
    mModMax[i].store(engine->ECModParameters[i]->param.getCurrentMax(), std::memory_order_relaxed);
  }
  for (int i = 0; i < consts::NUM_LFOS; ++i) {
    mLfoMin[i].store(engine->LFOParameters[i]->frequency->getCurrentMin(), std::memory_order_relaxed);
    mLfoMax[i].store(engine->LFOParameters[i]->frequency->getCurrentMax(), std::memory_order_relaxed);
  }

  mEngine = std::move(engine);
  mLive.store(mEngine.get(), std::memory_order_release);
  mPrepared.store(true, std::memory_order_release);
  return true;
}

bool EC2Adapter::setSamplePaths(const std::vector<std::string> &samplePaths) {
  if (!mPrepared.load(std::memory_order_acquire)) return false;

  std::unique_ptr<ecSynth> engine = buildEngine(samplePaths);
  if (!engine) return false;

  mPaths = samplePaths;
  publish(std::move(engine));
  return true;
}

void EC2Adapter::publish(std::unique_ptr<ecSynth> next) {
  std::lock_guard<std::mutex> swap(mSwapMutex);
  ecSynth *raw = next.get();

  mRetired.push_back(std::move(mEngine));
  mEngine = std::move(next);
  mLive.store(raw, std::memory_order_release);

  // Let the audio thread move past the swap before releasing older generations.
  if (mAudioRunning.load(std::memory_order_acquire)) waitForBlockTicks(2);
  while (mRetired.size() > kMaxRetired) mRetired.erase(mRetired.begin());
}

void EC2Adapter::waitForBlockTicks(int ticks) {
  const uint64_t start = mBlockTick.load(std::memory_order_acquire);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxWaitMs);
  while (mBlockTick.load(std::memory_order_acquire) < start + static_cast<uint64_t>(ticks)) {
    if (std::chrono::steady_clock::now() > deadline) return;
    std::this_thread::yield();
  }
}

void EC2Adapter::setParameter(int paramIndex, float value) {
  if (paramIndex < 0 || paramIndex >= consts::NUM_PARAMS) return;
  mParams[static_cast<size_t>(paramIndex)].store(value, std::memory_order_relaxed);
}

void EC2Adapter::setParameterRange(int paramIndex, float min, float max) {
  if (paramIndex < 0 || paramIndex >= consts::NUM_PARAMS) return;
  const size_t index = static_cast<size_t>(paramIndex);
  mParamMin[index].store(min, std::memory_order_relaxed);
  mParamMax[index].store(max, std::memory_order_relaxed);
}

void EC2Adapter::setModDepth(int paramIndex, float value) {
  if (paramIndex < 0 || paramIndex >= consts::NUM_PARAMS) return;
  mModDepths[static_cast<size_t>(paramIndex)].store(value, std::memory_order_relaxed);
}

void EC2Adapter::setModRange(int paramIndex, float min, float max) {
  if (paramIndex < 0 || paramIndex >= consts::NUM_PARAMS) return;
  const size_t index = static_cast<size_t>(paramIndex);
  mModMin[index].store(min, std::memory_order_relaxed);
  mModMax[index].store(max, std::memory_order_relaxed);
}

void EC2Adapter::setModLfo(int paramIndex, int lfoIndex) {
  if (paramIndex < 0 || paramIndex >= consts::NUM_PARAMS) return;
  mModLfo[static_cast<size_t>(paramIndex)].store(lfoIndex, std::memory_order_relaxed);
}

void EC2Adapter::setLfo(int lfoIndex, int field, float value) {
  if (lfoIndex < 0 || lfoIndex >= consts::NUM_LFOS || field < 0 || field >= 4) return;
  mLfo[static_cast<size_t>(lfoIndex) * 4 + static_cast<size_t>(field)].store(
    value, std::memory_order_relaxed);
}

void EC2Adapter::setLfoFrequencyRange(int lfoIndex, float min, float max) {
  if (lfoIndex < 0 || lfoIndex >= consts::NUM_LFOS) return;
  mLfoMin[static_cast<size_t>(lfoIndex)].store(min, std::memory_order_relaxed);
  mLfoMax[static_cast<size_t>(lfoIndex)].store(max, std::memory_order_relaxed);
}

float EC2Adapter::parameter(int paramIndex) const {
  if (paramIndex < 0 || paramIndex >= consts::NUM_PARAMS) return 0.0f;
  return mParams[static_cast<size_t>(paramIndex)].load(std::memory_order_relaxed);
}

bool EC2Adapter::process(float *outL, float *outR, int frames, float gain) {
  // Publish a tick first: setSamplePaths() waits on this to know a block passed.
  mBlockTick.fetch_add(1, std::memory_order_release);
  mAudioRunning.store(true, std::memory_order_release);

  // An engine rebuild touches the shared IO object and the parameter objects.
  // Take the lock if it is free, otherwise skip this block: loading a sample is
  // a user action, so a brief silence is better than a race.
  std::unique_lock<std::mutex> swap(mSwapMutex, std::try_to_lock);
  if (!swap.owns_lock()) return false;

  ecSynth *engine = mLive.load(std::memory_order_acquire);
  if (!engine || frames != mBlockSize) return false;

  // Audio thread is the only writer of al::Parameter (fixes H.6).
  for (int i = 0; i < consts::NUM_PARAMS; ++i)
  {
    const size_t idx = static_cast<size_t>(i);
    engine->ECParameters[i]->setCurrentMin(mParamMin[idx].load(std::memory_order_relaxed));
    engine->ECParameters[i]->setCurrentMax(mParamMax[idx].load(std::memory_order_relaxed));
    engine->ECParameters[i]->setParam(mParams[idx].load(std::memory_order_relaxed));

    engine->ECModParameters[i]->param.setCurrentMin(mModMin[idx].load(std::memory_order_relaxed));
    engine->ECModParameters[i]->param.setCurrentMax(mModMax[idx].load(std::memory_order_relaxed));
    engine->ECModParameters[i]->param.setParam(mModDepths[idx].load(std::memory_order_relaxed));
    engine->ECModParameters[i]->lfoMenu.set(mModLfo[idx].load(std::memory_order_relaxed));
  }

  // LFO controls: 0 shape, 1 polarity, 2 frequency, 3 duty.
  for (int i = 0; i < consts::NUM_LFOS; ++i)
  {
    const size_t base = static_cast<size_t>(i) * 4;
    engine->LFOParameters[i]->shape->set(
      static_cast<int>(mLfo[base + 0].load(std::memory_order_relaxed)));
    engine->LFOParameters[i]->polarity->set(
      static_cast<int>(mLfo[base + 1].load(std::memory_order_relaxed)));
    engine->LFOParameters[i]->frequency->setCurrentMin(mLfoMin[i].load(std::memory_order_relaxed));
    engine->LFOParameters[i]->frequency->setCurrentMax(mLfoMax[i].load(std::memory_order_relaxed));
    engine->LFOParameters[i]->frequency->setParam(mLfo[base + 2].load(std::memory_order_relaxed));
    engine->LFOParameters[i]->duty->set(mLfo[base + 3].load(std::memory_order_relaxed));
  }

  mIO.zeroOut();
  mIO.frame(0);
  engine->onProcess(mIO);

  float peakL = 0.0f, peakR = 0.0f;
  const float *ioL = mIO.outBuffer(0);
  const float *ioR = mIO.outBuffer(1);
  for (int i = 0; i < frames; ++i) {
    peakL = std::max(peakL, std::fabs(ioL[i]));
    peakR = std::max(peakR, std::fabs(ioR[i]));
  }
  mPeakL.store(peakL, std::memory_order_relaxed);
  mPeakR.store(peakR, std::memory_order_relaxed);

  if (engine->mModClip >= 0 && engine->mModClip < static_cast<int>(engine->soundClip.size())) {
    const auto &clip = engine->soundClip[engine->mModClip];
    if (clip && clip->frames > 0) {
      float position = engine->getCurrentIndex() / static_cast<float>(clip->frames);
      position = std::fmod(position, 1.0f);
      if (position < 0.0f) position += 1.0f;
      mScanPos.store(position, std::memory_order_release);
    }
  }

  // Audio thread is the only writer here, and it owns the grain list, so the
  // snapshot is safe without touching the list from another thread.
  int grainCount = 0;
  float grainIdx[kMaxGrainDots];
  engine->copyActiveGrainIndicies(grainIdx, &grainCount, kMaxGrainDots);
  grainCount = std::min(grainCount, kMaxGrainDots);
  for (int i = 0; i < grainCount; ++i) mGrainIdx[i].store(grainIdx[i], std::memory_order_relaxed);
  mGrainCount.store(grainCount, std::memory_order_release);

  const float *l = mIO.outBuffer(0);
  const float *r = mIO.outBuffer(1);
  for (int i = 0; i < frames; ++i) {
    outL[i] += l[i] * gain;
    outR[i] += r[i] * gain;
  }
  return true;
}

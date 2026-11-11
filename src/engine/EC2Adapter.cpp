// EC2Adapter.cpp, see EC2Adapter.h for the threading contract.

#include "EC2Adapter.h"

#include <chrono>
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
  for (int i = 0; i < consts::NUM_PARAMS; ++i)
    mParams[i].store(engine->ECParameters[i]->getParam(), std::memory_order_relaxed);

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

float EC2Adapter::parameter(int paramIndex) const {
  if (paramIndex < 0 || paramIndex >= consts::NUM_PARAMS) return 0.0f;
  return mParams[static_cast<size_t>(paramIndex)].load(std::memory_order_relaxed);
}

bool EC2Adapter::process(float *outL, float *outR, int frames, float gain) {
  // Publish a tick first: setSamplePaths() waits on this to know a block passed.
  mBlockTick.fetch_add(1, std::memory_order_release);
  mAudioRunning.store(true, std::memory_order_release);

  ecSynth *engine = mLive.load(std::memory_order_acquire);
  if (!engine || frames != mBlockSize) return false;

  // Audio thread is the only writer of al::Parameter (fixes H.6).
  for (int i = 0; i < consts::NUM_PARAMS; ++i)
    engine->ECParameters[i]->setParam(mParams[static_cast<size_t>(i)].load(std::memory_order_relaxed));

  mIO.zeroOut();
  mIO.frame(0);
  engine->onProcess(mIO);

  const float *l = mIO.outBuffer(0);
  const float *r = mIO.outBuffer(1);
  for (int i = 0; i < frames; ++i) {
    outL[i] += l[i] * gain;
    outR[i] += r[i] * gain;
  }
  return true;
}
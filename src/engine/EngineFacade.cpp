#include "EngineFacade.h"

#include "VoiceManager.h"

static_assert(ec2param::GrainRate == consts::GRAIN_RATE, "parameter order changed upstream");
static_assert(ec2param::Async == consts::ASYNC, "parameter order changed upstream");
static_assert(ec2param::Interm == consts::INTERM, "parameter order changed upstream");
static_assert(ec2param::Streams == consts::STREAMS, "parameter order changed upstream");
static_assert(ec2param::Playback == consts::PLAYBACK, "parameter order changed upstream");
static_assert(ec2param::FilterCenter == consts::FILTER_CENTER, "parameter order changed upstream");
static_assert(ec2param::Resonance == consts::RESONANCE, "parameter order changed upstream");
static_assert(ec2param::SoundFile == consts::SOUND_FILE, "parameter order changed upstream");
static_assert(ec2param::ScanBegin == consts::SCAN_BEGIN, "parameter order changed upstream");
static_assert(ec2param::ScanRange == consts::SCAN_RANGE, "parameter order changed upstream");
static_assert(ec2param::ScanSpeed == consts::SCAN_SPEED, "parameter order changed upstream");
static_assert(ec2param::GrainDuration == consts::GRAIN_DUR, "parameter order changed upstream");
static_assert(ec2param::Envelope == consts::ENVELOPE, "parameter order changed upstream");
static_assert(ec2param::Pan == consts::PAN, "parameter order changed upstream");
static_assert(ec2param::Amplitude == consts::AMPLITUDE, "parameter order changed upstream");

struct Engine::Impl {
  VoiceManager voices;
  int blockSize = 0;
  bool prepared = false;
};

Engine::Engine() : m(new Impl()) {}

Engine::~Engine() = default;

bool Engine::prepare(double sampleRate, int blockSizeIn, int voiceCount,
                     const std::vector<std::string> &samples) {
  m->blockSize = blockSizeIn;
  m->prepared = m->voices.prepare(sampleRate, blockSizeIn, voiceCount, samples);
  return m->prepared;
}

bool Engine::isPrepared() const { return m->prepared; }

int Engine::blockSize() const { return m->blockSize; }

void Engine::setParameter(int paramIndex, float value) { m->voices.setParameter(paramIndex, value); }

void Engine::setParameterRange(int paramIndex, float min, float max) { m->voices.setParameterRange(paramIndex, min, max); }

void Engine::setModDepth(int paramIndex, float value) { m->voices.setModDepth(paramIndex, value); }

void Engine::setModRange(int paramIndex, float min, float max) { m->voices.setModRange(paramIndex, min, max); }

void Engine::setModLfo(int paramIndex, int lfoIndex) { m->voices.setModLfo(paramIndex, lfoIndex); }

void Engine::setLfo(int lfoIndex, int field, float value) { m->voices.setLfo(lfoIndex, field, value); }

void Engine::setLfoFrequencyRange(int lfoIndex, float min, float max) { m->voices.setLfoFrequencyRange(lfoIndex, min, max); }

float Engine::scanPosition() const { return m->voices.scanPosition(); }

void Engine::noteOn(int midiNote, float velocity, bool pitchFromNote) { m->voices.noteOn(midiNote, velocity, pitchFromNote); }

float Engine::parameter(int paramIndex) const { return m->voices.parameter(paramIndex); }

void Engine::noteOff(int midiNote) { m->voices.noteOff(midiNote); }

void Engine::allNotesOff() { m->voices.allNotesOff(); }

void Engine::render(float *outL, float *outR, int frames) {
  if (!m->prepared) return;
  m->voices.render(outL, outR, frames);
}

bool Engine::setSamples(const std::vector<std::string> &paths) {
  if (!m->prepared) return false;
  return m->voices.loadSample(paths);
}

bool Engine::loadSample(const std::string &path) {
  if (!m->prepared) return false;
  return m->voices.loadSample(path);
}

int Engine::grains(float *out, int max) const {
  if (!m->prepared) return 0;
  return m->voices.grains(out, max);
}

void Engine::meters(float &peakL, float &peakR, int &activeGrains) const {
  peakL = peakR = 0.0f;
  activeGrains = 0;
  if (!m->prepared) return;
  m->voices.meters(peakL, peakR, activeGrains);
}

const float *Engine::waveform(int &frames, int &channels) const {
  frames = 0;
  channels = 0;
  if (!m->prepared) return nullptr;

  const auto *clip = m->voices.firstSample();
  if (!clip || !clip->data) return nullptr;
  frames = static_cast<int>(clip->frames);
  channels = clip->channels;
  return clip->data;
}

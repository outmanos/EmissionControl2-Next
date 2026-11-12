#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "IPlug_include_in_plug_hdr.h"

#include "EngineFacade.h"
#include "OscReceiver.h"

using namespace iplug;
using namespace igraphics;

enum EParams
{
  kGain = 0,
  // EC2's fifteen parameters, then their modulation depth and LFO routing,
  // then the six LFOs' four controls each. Index arithmetic keeps the table in
  // one place instead of seventy named constants.
  kParamBase = 1,
  kModBase = kParamBase + 15,
  kModLfoBase = kModBase + 15,
  kLfoBase = kModLfoBase + 15,
  kFreeRun = kLfoBase + 24,
  kNumParams = kFreeRun + 1
};

constexpr int kEcParamCount = 15;
constexpr int kLfoCount = 6;

const char* Ec2ParamName(int index);

class EC2Next final : public iplug::Plugin
{
public:
  EC2Next(const InstanceInfo& info);
  ~EC2Next() override;

  void OnReset() override;
  void OnParentWindowResize(int width, int height) override;
  void OnParamChange(int paramIdx) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

  bool engineReady() const { return mEngine.isPrepared(); }
  int notesSeen() const { return mNotesSeen.load(); }
  const float* waveform(int& frames, int& channels) const { return mEngine.waveform(frames, channels); }
  void meters(float& l, float& r, int& grains) const { mEngine.meters(l, r, grains); }
  int grains(float* out, int max) const { return mEngine.grains(out, max); }
  float scanPosition() const { return mEngine.scanPosition(); }
  int readScope(float* left, float* right, int maxFrames) const;
  double sampleRate() const { return mSampleRate; }
  bool loadSample(const std::string& path);
  /// Appends a sample to the bank, or clears it when path is empty.
  bool addSample(const std::string& path);
  void clearSamples();
  void refreshSampleName();
  int sampleCount() const { return static_cast<int>(mSamplePaths.size()); }
  const char* sampleName() const
  {
    if (mSampleNames.empty()) return "No Sound Files";
    const int raw = static_cast<int>(std::round(GetParam(kParamBase + 7)->Value())) - 1;
    const int index = std::max(0, std::min(static_cast<int>(mSampleNames.size()) - 1, raw));
    return mSampleNames[static_cast<size_t>(index)].c_str();
  }

  int editorSkin() const { return mEditorSkin; }
  double editorScale() const { return mEditorScale; }
  void setEditorSkin(int skin);
  void setEditorScale(double scale, bool resize = true);
  void syncEditorScale(double scale);
  bool saveEditorDefaults();

  void parameterRange(int paramIdx, double& min, double& max) const;
  void setParameterRange(int paramIdx, double min, double max);

  bool savePresetFile(const char* path);
  bool loadPresetFile(const char* path);

  void refreshPresetBank();
  void loadPreset(int index) { loadPresetAt(index); }
  void nextPreset();
  void previousPreset();
  void saveNewPreset();
  const char* presetName() const { return mPresetName.c_str(); }
  int presetCount() const { return static_cast<int>(mPresetFiles.size()); }
  int presetIndex() const { return mPresetIndex; }
  int lastNote() const { return mLastNote.load(); }

private:
  struct Range
  {
    double min = 0.0;
    double max = 1.0;
  };

  static constexpr int kScopeBlock = 512;
  static constexpr int kScopeSlots = 1024;

  struct ScopeSlot
  {
    enum State { Empty, Writing, Ready, Reading };
    std::atomic<int> state{Empty};
    std::atomic<uint64_t> sequence{0};
    std::array<float, kScopeBlock> left{};
    std::array<float, kScopeBlock> right{};
  };

  void applyParameters();
  void handleOsc(const std::string& address, double value);
  void loadEditorDefaults();
  void refill();
  void captureScope();
  Range* rangeFor(int paramIdx);
  const Range* rangeFor(int paramIdx) const;

  bool mFreeRunActive = false;
  OscReceiver mOsc;

  void loadPresetAt(int index);
  std::vector<std::string> mPresetFiles;
  std::string mPresetName;
  std::string mPresetDir;
  int mPresetIndex = -1;

  std::atomic<int> mNotesSeen{0};
  std::atomic<int> mLastNote{-1};

  Engine mEngine;
  std::vector<float> mLeft;
  std::vector<float> mRight;
  int mRead = 0;
  double mSampleRate = 48000.0;
  std::string mSampleName = "No Sound Files";
  std::vector<std::string> mSampleNames;
  std::vector<std::string> mSamplePaths;
  int editorWidth() const { return mEditorWidth; }
  int editorHeight() const { return mEditorHeight; }
  void setEditorSize(int width, int height);

  int mEditorSkin = 2; // Studio, until the user picks another theme
  double mEditorScale = 1.0;
  int mLastPixelWidth = 0;
  int mLastPixelHeight = 0;
  int mEditorWidth = PLUG_WIDTH;
  int mEditorHeight = PLUG_HEIGHT;
  static constexpr int kMinEditorWidth = 1100;
  static constexpr int kMinEditorHeight = 660;

  std::array<Range, kEcParamCount> mParameterRanges;
  std::array<Range, kEcParamCount> mModRanges;
  std::array<Range, kLfoCount> mLfoRanges;

  mutable std::array<ScopeSlot, kScopeSlots> mScope;
  uint32_t mScopeWrite = 0;
  std::atomic<uint64_t> mScopeSequence{0};
};

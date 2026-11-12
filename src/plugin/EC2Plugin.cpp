#include "EC2Plugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
#include "EC2Editor.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
void loadBundledSndfile()
{
#ifdef _WIN32
  static bool attempted = false;
  if (attempted) return;
  attempted = true;

  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&loadBundledSndfile), &self))
    return;

  wchar_t path[MAX_PATH];
  const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) return;

  std::wstring dir(path, length);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return;
  dir.resize(slash + 1);

  LoadLibraryW((dir + L"sndfile.dll").c_str());
#endif
}

constexpr int kEngineBlock = 512;
constexpr int kVoices = 6;
constexpr uint32_t kRangeMagic = 0x45433252;
constexpr uint32_t kRangeVersion = 2;

std::filesystem::path editorDefaultsPath()
{
  const char* base = std::getenv("LOCALAPPDATA");
  return base ? std::filesystem::path(base) / "EmissionControl2-Next" / "editor.cfg"
              : std::filesystem::current_path() / "editor.cfg";
}

std::string samplePath()
{
  if (const char* env = std::getenv("EC2_SAMPLE"))
    return env;
  return "C:\\Dev\\GitHub\\EmissionControl2-Next\\externalResources\\samples\\440sine48k.wav";
}

struct ParamSpec
{
  const char* name;
  double def;
  double min;
  double max;
  double currentMin;
  double currentMax;
  double step;
  bool integer;
  const char* unit;
};

const ParamSpec kSpecs[kEcParamCount] = {
  {"Grain Rate", 1.0, 0.1, 100.0, 0.1, 100.0, 0.01, false, "Hz"},
  {"Asynchronicity", 0.0, 0.0, 1.0, 0.0, 1.0, 0.001, false, ""},
  {"Intermittency", 0.0, 0.0, 1.0, 0.0, 1.0, 0.001, false, ""},
  {"Streams", 1.0, 1.0, 12.0, 1.0, 12.0, 1.0, true, ""},
  {"Playback Rate", 1.0, -2.0, 2.0, -2.0, 2.0, 0.001, false, ""},
  {"Filter Center", 440.0, 20.0, 24000.0, 60.0, 5000.0, 1.0, false, "Hz"},
  {"Resonance", 0.0, 0.0, 1.0, 0.0, 1.0, 0.001, false, ""},
  {"Sound File", 1.0, 1.0, 64.0, 1.0, 1.0, 1.0, true, ""},
  {"Scan Begin", 0.0, 0.0, 1.0, 0.0, 1.0, 0.001, false, ""},
  {"Scan Range", 0.5, -1.0, 1.0, -1.0, 1.0, 0.001, false, ""},
  {"Scan Speed", 1.0, -2.0, 2.0, -2.0, 2.0, 0.001, false, ""},
  {"Grain Duration", 30.0, 0.046, 10000.0, 0.046, 1000.0, 0.1, false, "ms"},
  {"Envelope Shape", 0.5, 0.0, 1.0, 0.0, 1.0, 0.001, false, ""},
  {"Pan", 0.0, -1.0, 1.0, -1.0, 1.0, 0.001, false, ""},
  {"Amplitude", -6.0, -60.0, 24.0, -60.0, 24.0, 0.1, false, "dB"},
};
} // namespace

const char* Ec2ParamName(int index)
{
  if (index < 0 || index >= kEcParamCount) return "";
  return kSpecs[index].name;
}

EC2Next::EC2Next(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, 1))
{
  loadBundledSndfile();

  GetParam(kGain)->InitDouble("Master", 0.7, 0.0, 1.0, 0.001, "");
  GetParam(kFreeRun)->InitBool("Free Run", false);

  for (int i = 0; i < kEcParamCount; ++i)
  {
    const ParamSpec& s = kSpecs[i];
    if (s.integer)
      GetParam(kParamBase + i)->InitInt(s.name, static_cast<int>(s.def), static_cast<int>(s.min),
                                        static_cast<int>(s.max), s.unit);
    else
      GetParam(kParamBase + i)->InitDouble(s.name, s.def, s.min, s.max, s.step, s.unit);

    GetParam(kModBase + i)->InitDouble((std::string(s.name) + " Mod").c_str(), 0.0, 0.0, 1.0, 0.001, "");
    GetParam(kModLfoBase + i)->InitInt((std::string(s.name) + " LFO").c_str(), 0, 0, kLfoCount - 1, "");
    mParameterRanges[i] = {s.currentMin, s.currentMax};
    mModRanges[i] = {0.0, 1.0};
  }

  for (int lfo = 0; lfo < kLfoCount; ++lfo)
  {
    const int base = kLfoBase + lfo * 4;
    const std::string n = "LFO " + std::to_string(lfo + 1);
    GetParam(base + 0)->InitInt((n + " Shape").c_str(), 0, 0, 4, "");
    GetParam(base + 1)->InitInt((n + " Polarity").c_str(), 0, 0, 2, "");
    GetParam(base + 2)->InitDouble((n + " Frequency").c_str(), 1.0, 0.01, 30.0, 0.001, "Hz");
    GetParam(base + 3)->InitDouble((n + " Duty").c_str(), 0.5, 0.0, 1.0, 0.001, "");
    mLfoRanges[lfo] = {0.01, 30.0};
  }

  loadEditorDefaults();

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    // Free-form size: the layout is drawn from the canvas dimensions, so width
    // and height change independently. The canvas is in logical units and the
    // scale is the user's zoom, never a value derived from the window, because a
    // derived scale makes every resize report itself again and the window grows
    // without bound.
    return MakeGraphics(*this, mEditorWidth, mEditorHeight, PLUG_FPS,
                        static_cast<float>(mEditorScale));
  };

  mLayoutFunc = [this](IGraphics* pGraphics) { BuildEC2Editor(pGraphics, this); };
#endif
}

void EC2Next::setEditorSize(int width, int height)
{
#if IPLUG_EDITOR
  IGraphics* ui = GetUI();
  if (!ui) return;

  // Moving a window can deliver size notifications with identical dimensions.
  // Every Resize reallocates the backing bitmap, so ignoring repeats is what
  // stops memory climbing while the window is dragged around.
  if (width == mLastPixelWidth && height == mLastPixelHeight) return;
  mLastPixelWidth = width;
  mLastPixelHeight = height;

  // One invariant everywhere: window pixels = canvas * zoom. The host reports
  // pixels, so the canvas is pixels divided by the same zoom that Resize is
  // given. Dividing by a different factor (the platform scale) is what left
  // black bands, and passing 1.0 while the zoom was 1.25 is what made a bigger
  // window render at 100%.
  const float zoom = std::max(0.5f, std::min(2.0f, static_cast<float>(mEditorScale)));
  const int canvasWidth = std::max(kMinEditorWidth, static_cast<int>(std::lround(width / zoom)));
  const int canvasHeight = std::max(kMinEditorHeight, static_cast<int>(std::lround(height / zoom)));

  if (canvasWidth == ui->Width() && canvasHeight == ui->Height()) return;

  mEditorWidth = canvasWidth;
  mEditorHeight = canvasHeight;
  ui->Resize(mEditorWidth, mEditorHeight, zoom, false);
  ui->SetAllControlsDirty();
#else
  (void)width;
  (void)height;
#endif
}

void EC2Next::OnParentWindowResize(int width, int height)
{
  // Deliberately does nothing. Driving the canvas from the host window size meant
  // calling IGraphics::Resize on every host notification, and each call
  // reallocates the backing bitmap, which leaked while the window was dragged
  // (5 GB in a host here). The canvas stays at the size the editor was created
  // with and the framework scales it to the window, which is stable.
  (void)width;
  (void)height;
}

void EC2Next::setEditorSkin(int skin)
{
  mEditorSkin = std::max(0, std::min(2, skin));
  DirtyParametersFromUI();
  if (GetUI()) GetUI()->SetAllControlsDirty();
}

void EC2Next::syncEditorScale(double scale)
{
  const double clamped = std::max(0.5, std::min(2.0, scale));
  if (std::abs(clamped - mEditorScale) < 1e-6) return;
  mEditorScale = clamped;
  DirtyParametersFromUI();
}

void EC2Next::setEditorScale(double scale, bool resize)
{
  mEditorScale = std::max(0.5, std::min(2.0, scale));
  DirtyParametersFromUI();
#if IPLUG_EDITOR
  if (resize && GetUI())
  {
    // Same canvas, new zoom: the host grows the window to canvas * zoom,
    // and OnParentWindowResize then derives the same canvas back.
    GetUI()->Resize(GetUI()->Width(), GetUI()->Height(), static_cast<float>(mEditorScale), true);
    GetUI()->SetAllControlsDirty();
  }
#endif
}

void EC2Next::loadEditorDefaults()
{
  std::ifstream file(editorDefaultsPath());
  int skin = 0;
  double scale = 1.0;
  int width = PLUG_WIDTH, height = PLUG_HEIGHT;
  if (file >> skin >> scale && skin >= 0 && skin <= 2 && std::isfinite(scale))
  {
    mEditorSkin = skin;
    mEditorScale = std::max(0.5, std::min(2.0, scale));
    if (file >> width >> height && width > 0 && height > 0)
    {
      mEditorWidth = std::max(kMinEditorWidth, width);
      mEditorHeight = std::max(kMinEditorHeight, height);
    }
  }
}

bool EC2Next::saveEditorDefaults()
{
#if IPLUG_EDITOR
  if (GetUI()) syncEditorScale(GetUI()->GetDrawScale());
#endif
  std::error_code error;
  const std::filesystem::path path = editorDefaultsPath();
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;
  std::ofstream file(path, std::ios::trunc);
  file << mEditorSkin << ' ' << mEditorScale << ' ' << mEditorWidth << ' ' << mEditorHeight << '\n';
  return static_cast<bool>(file);
}

void EC2Next::refreshPresetBank()
{
  mPresetFiles.clear();

  std::error_code ec;
  const char* base = std::getenv("LOCALAPPDATA");
  std::filesystem::path dir = base ? std::filesystem::path(base) / "EmissionControl2-Next" / "presets"
                                   : std::filesystem::current_path() / "presets";
  std::filesystem::create_directories(dir, ec);
  mPresetDir = dir.string();

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
  {
    if (entry.is_regular_file() && entry.path().extension() == ".ec2p")
      mPresetFiles.push_back(entry.path().string());
  }
  std::sort(mPresetFiles.begin(), mPresetFiles.end());

  if (mPresetIndex >= static_cast<int>(mPresetFiles.size())) mPresetIndex = static_cast<int>(mPresetFiles.size()) - 1;
  if (mPresetIndex < 0 && !mPresetFiles.empty()) mPresetIndex = 0;
  mPresetName = mPresetIndex >= 0 ? std::filesystem::path(mPresetFiles[mPresetIndex]).stem().string() : "(no presets)";
}

void EC2Next::loadPresetAt(int index)
{
  if (index < 0 || index >= static_cast<int>(mPresetFiles.size())) return;
  mPresetIndex = index;
  mPresetName = std::filesystem::path(mPresetFiles[index]).stem().string();
  loadPresetFile(mPresetFiles[index].c_str());
}

void EC2Next::nextPreset()
{
  if (mPresetFiles.empty()) { refreshPresetBank(); return; }
  loadPresetAt((mPresetIndex + 1) % static_cast<int>(mPresetFiles.size()));
}

void EC2Next::previousPreset()
{
  if (mPresetFiles.empty()) { refreshPresetBank(); return; }
  const int n = static_cast<int>(mPresetFiles.size());
  loadPresetAt((mPresetIndex - 1 + n) % n);
}

void EC2Next::saveNewPreset()
{
  refreshPresetBank();
  mOsc.start(16447, [this](const std::string& a, double v) { handleOsc(a, v); });

  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);

  char name[64];
  snprintf(name, sizeof(name), "preset-%04d%02d%02d-%02d%02d%02d.ec2p",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

  const std::string path = mPresetDir + "/" + name;
  if (savePresetFile(path.c_str()))
  {
    refreshPresetBank();
    for (size_t i = 0; i < mPresetFiles.size(); ++i)
    {
      if (std::filesystem::path(mPresetFiles[i]).filename() == std::filesystem::path(path).filename())
      {
        mPresetIndex = static_cast<int>(i);
        mPresetName = std::filesystem::path(path).stem().string();
        break;
      }
    }
  }
}

EC2Next::~EC2Next() { mOsc.stop(); }

void EC2Next::handleOsc(const std::string& address, double value)
{
  static const char* kAddresses[kEcParamCount] = {
    "/GrainRate", "/Asynchronicity", "/Intermittency", "/Streams", "/PlaybackRate",
    "/FilterCenter", "/Resonance", "/SoundFile", "/ScanBegin", "/ScanRange",
    "/ScanSpeed", "/GrainDuration", "/Envelope", "/Pan", "/Amplitude"};

  for (int i = 0; i < kEcParamCount; ++i)
  {
    const int param = kParamBase + i;
    if (address == kAddresses[i])
    {
      GetParam(param)->Set(value);
      SendParameterValueFromUI(param, GetParam(param)->GetNormalized());
      return;
    }

    if (address == std::string("/mod") + (kAddresses[i] + 1))
    {
      const int mod = kModBase + i;
      GetParam(mod)->Set(value);
      SendParameterValueFromUI(mod, GetParam(mod)->GetNormalized());
      return;
    }
  }

  for (int lfo = 0; lfo < kLfoCount; ++lfo)
  {
    if (address == "/lfo" + std::to_string(lfo + 1))
    {
      const int freq = kLfoBase + lfo * 4 + 2;
      GetParam(freq)->Set(value);
      SendParameterValueFromUI(freq, GetParam(freq)->GetNormalized());
      return;
    }
  }
}

bool EC2Next::loadSample(const std::string& path)
{
  mSamplePaths.assign(1, path);
  const bool ok = mEngine.setSamples(mSamplePaths);
  if (ok)
  {
    refreshSampleName();
    // Replacing the bank leaves one clip, so point the selector at it.
    const int param = kParamBase + 7;
    GetParam(param)->Set(1.0);
    SendParameterValueFromUI(param, GetParam(param)->GetNormalized());
  }
  return ok;
}

bool EC2Next::addSample(const std::string& path)
{
  if (path.empty()) return false;
  mSamplePaths.push_back(path);
  const bool ok = mEngine.setSamples(mSamplePaths);
  if (ok)
  {
    refreshSampleName();
    // Select the clip just added so it is audible immediately.
    const double index = static_cast<double>(mSamplePaths.size());
    const int param = kParamBase + 7;
    GetParam(param)->Set(index);
    SendParameterValueFromUI(param, GetParam(param)->GetNormalized());
  }
  return ok;
}

void EC2Next::clearSamples()
{
  mSamplePaths.clear();
  mEngine.setSamples(mSamplePaths);
  refreshSampleName();
}

void EC2Next::refreshSampleName()
{
  mSampleNames.clear();
  for (const std::string& path : mSamplePaths)
    mSampleNames.push_back(std::filesystem::path(path).filename().string());

  if (mSamplePaths.empty())
  {
    mSampleName = "No Sound Files";
    setParameterRange(kParamBase + 7, 1.0, 1.0);
    return;
  }

  mSampleName = mSampleNames.front();
  const double count = static_cast<double>(mSamplePaths.size());
  setParameterRange(kParamBase + 7, 1.0, std::max(1.0, count));
}
bool EC2Next::savePresetFile(const char* path)
{
  if (!path) return false;
  IByteChunk chunk;
  if (!SerializeState(chunk)) return false;

  FILE* file = nullptr;
  if (fopen_s(&file, path, "wb") != 0 || !file) return false;
  fwrite(chunk.GetData(), 1, static_cast<size_t>(chunk.Size()), file);
  fclose(file);
  return true;
}

bool EC2Next::loadPresetFile(const char* path)
{
  if (!path) return false;

  FILE* file = nullptr;
  if (fopen_s(&file, path, "rb") != 0 || !file) return false;
  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size <= 0) { fclose(file); return false; }

  std::vector<char> bytes(static_cast<size_t>(size));
  const size_t read = fread(bytes.data(), 1, bytes.size(), file);
  fclose(file);
  if (read != bytes.size()) return false;

  IByteChunk chunk;
  chunk.PutBytes(bytes.data(), static_cast<int>(bytes.size()));
  if (UnserializeState(chunk, 0) < 0) return false;
  applyParameters();
  return true;
}

EC2Next::Range* EC2Next::rangeFor(int paramIdx)
{
  if (paramIdx >= kParamBase && paramIdx < kParamBase + kEcParamCount)
    return &mParameterRanges[paramIdx - kParamBase];
  if (paramIdx >= kModBase && paramIdx < kModBase + kEcParamCount)
    return &mModRanges[paramIdx - kModBase];
  if (paramIdx >= kLfoBase && paramIdx < kLfoBase + kLfoCount * 4 && (paramIdx - kLfoBase) % 4 == 2)
    return &mLfoRanges[(paramIdx - kLfoBase) / 4];
  return nullptr;
}

const EC2Next::Range* EC2Next::rangeFor(int paramIdx) const
{
  return const_cast<EC2Next*>(this)->rangeFor(paramIdx);
}

void EC2Next::parameterRange(int paramIdx, double& min, double& max) const
{
  if (const Range* range = rangeFor(paramIdx))
  {
    min = range->min;
    max = range->max;
    return;
  }
  min = 0.0;
  max = 1.0;
}

void EC2Next::setParameterRange(int paramIdx, double min, double max)
{
  Range* range = rangeFor(paramIdx);
  auto* param = GetParam(paramIdx);
  if (!range || !param || !std::isfinite(min) || !std::isfinite(max)) return;

  if (paramIdx == kParamBase + 7)
  {
    // Sound File indexes the loaded bank; it used to be pinned to 1..1
    min = 1.0;
    max = std::max(1.0, std::min(param->GetMax(), max));
  }
  else
  {
    if (param->GetStepped())
    {
      min = std::round(min);
      max = std::round(max);
    }
    const double minimum = param->GetStepped() ? 1.0 : std::max(1e-6, (param->GetMax() - param->GetMin()) * 1e-6);
    min = std::max(param->GetMin(), std::min(param->GetMax() - minimum, min));
    max = std::max(min + minimum, std::min(param->GetMax(), max));
  }
  range->min = min;
  range->max = max;

  const double value = std::max(min, std::min(max, param->Value()));
  if (value != param->Value())
  {
    const double norm = param->ToNormalized(value);
    param->SetNormalized(norm);
    SendParameterValueFromUI(paramIdx, norm);
  }

  if (paramIdx >= kParamBase && paramIdx < kParamBase + kEcParamCount)
    mEngine.setParameterRange(paramIdx - kParamBase, static_cast<float>(min), static_cast<float>(max));
  else if (paramIdx >= kModBase && paramIdx < kModBase + kEcParamCount)
    mEngine.setModRange(paramIdx - kModBase, static_cast<float>(min), static_cast<float>(max));
  else if (paramIdx >= kLfoBase && paramIdx < kLfoBase + kLfoCount * 4)
    mEngine.setLfoFrequencyRange((paramIdx - kLfoBase) / 4, static_cast<float>(min), static_cast<float>(max));
}

bool EC2Next::SerializeState(IByteChunk& chunk) const
{
  if (!iplug::Plugin::SerializeState(chunk)) return false;
  chunk.Put(&kRangeMagic);
  chunk.Put(&kRangeVersion);
  auto writeRanges = [&](const auto& ranges) {
    for (const Range& range : ranges)
    {
      chunk.Put(&range.min);
      chunk.Put(&range.max);
    }
  };
  writeRanges(mParameterRanges);
  writeRanges(mModRanges);
  writeRanges(mLfoRanges);
  const int32_t skin = mEditorSkin;
  chunk.Put(&skin);
  chunk.Put(&mEditorScale);
  return true;
}

int EC2Next::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = UnserializeParams(chunk, startPos);
  if (pos < 0 || pos == chunk.Size()) return pos;

  uint32_t magic = 0;
  int next = chunk.Get(&magic, pos);
  if (next < 0 || magic != kRangeMagic) return pos;
  pos = next;

  uint32_t version = 0;
  next = chunk.Get(&version, pos);
  if (next < 0 || version < 1 || version > kRangeVersion) return -1;
  pos = next;

  auto readRanges = [&](auto& ranges, int base, int stride) -> bool {
    for (size_t i = 0; i < ranges.size(); ++i)
    {
      double min = 0.0, max = 0.0;
      next = chunk.Get(&min, pos);
      if (next < 0) return false;
      pos = next;
      next = chunk.Get(&max, pos);
      if (next < 0) return false;
      pos = next;
      if (!std::isfinite(min) || !std::isfinite(max) || min > max) return false;
      setParameterRange(base + static_cast<int>(i) * stride, min, max);
    }
    return true;
  };

  if (!readRanges(mParameterRanges, kParamBase, 1)) return -1;
  if (!readRanges(mModRanges, kModBase, 1)) return -1;
  if (!readRanges(mLfoRanges, kLfoBase + 2, 4)) return -1;

  if (version >= 2)
  {
    int32_t skin = 0;
    double scale = 1.0;
    next = chunk.Get(&skin, pos);
    if (next < 0) return -1;
    pos = next;
    next = chunk.Get(&scale, pos);
    if (next < 0 || skin < 0 || skin > 2 || !std::isfinite(scale)) return -1;
    pos = next;
    setEditorSkin(skin);
    setEditorScale(scale);
  }
  return pos;
}

void EC2Next::OnReset()
{
  mSampleRate = GetSampleRate();

  if (!mEngine.prepare(mSampleRate, kEngineBlock, kVoices, {samplePath()}))
    return;

  mSamplePaths.assign(1, samplePath());
  refreshSampleName();
  mLeft.assign(kEngineBlock, 0.0f);
  mRight.assign(kEngineBlock, 0.0f);
  mRead = kEngineBlock;
  mScopeWrite = 0;
  mScopeSequence.store(0, std::memory_order_release);
  for (ScopeSlot& slot : mScope)
  {
    slot.sequence.store(0, std::memory_order_relaxed);
    slot.state.store(ScopeSlot::Empty, std::memory_order_release);
  }
  applyParameters();
  refreshPresetBank();
  mOsc.start(16447, [this](const std::string& a, double v) { handleOsc(a, v); });
}


void EC2Next::applyParameters()
{
  if (!mEngine.isPrepared()) return;

  for (int i = 0; i < kEcParamCount; ++i)
  {
    mEngine.setParameterRange(i, static_cast<float>(mParameterRanges[i].min),
                              static_cast<float>(mParameterRanges[i].max));
    mEngine.setParameter(i, static_cast<float>(GetParam(kParamBase + i)->Value()));
    mEngine.setModRange(i, static_cast<float>(mModRanges[i].min),
                        static_cast<float>(mModRanges[i].max));
    mEngine.setModDepth(i, static_cast<float>(GetParam(kModBase + i)->Value()));
    mEngine.setModLfo(i, static_cast<int>(GetParam(kModLfoBase + i)->Value()));
  }

  for (int lfo = 0; lfo < kLfoCount; ++lfo)
  {
    const int base = kLfoBase + lfo * 4;
    mEngine.setLfoFrequencyRange(lfo, static_cast<float>(mLfoRanges[lfo].min),
                                 static_cast<float>(mLfoRanges[lfo].max));
    mEngine.setLfo(lfo, 0, static_cast<float>(GetParam(base + 0)->Value()));
    mEngine.setLfo(lfo, 1, static_cast<float>(GetParam(base + 1)->Value()));
    mEngine.setLfo(lfo, 2, static_cast<float>(GetParam(base + 2)->Value()));
    mEngine.setLfo(lfo, 3, static_cast<float>(GetParam(base + 3)->Value()));
  }
}

void EC2Next::OnParamChange(int paramIdx)
{
  if (paramIdx == kGain) return;
  applyParameters();
}

void EC2Next::ProcessMidiMsg(const IMidiMsg& msg)
{
  const IMidiMsg::EStatusMsg status = msg.StatusMsg();

  if (status == IMidiMsg::kNoteOn && msg.mData2 > 0)
  {
    mNotesSeen.fetch_add(1);
    mLastNote.store(msg.mData1);
    mEngine.noteOn(msg.mData1, msg.mData2 / 127.0f);
  }
  else if (status == IMidiMsg::kNoteOff ||
           (status == IMidiMsg::kNoteOn && msg.mData2 == 0))
  {
    mEngine.noteOff(msg.mData1);
  }
  else if (status == IMidiMsg::kControlChange)
  {
    const int cc = msg.mData1;
    if (cc >= 20 && cc < 20 + kEcParamCount)
    {
      const int param = kParamBase + (cc - 20);
      const double norm = msg.mData2 / 127.0;
      GetParam(param)->SetNormalized(norm);
      SendParameterValueFromUI(param, norm);
    }
  }
}

void EC2Next::captureScope()
{
  ScopeSlot& slot = mScope[mScopeWrite++ % kScopeSlots];
  int expected = ScopeSlot::Empty;
  if (!slot.state.compare_exchange_strong(expected, ScopeSlot::Writing, std::memory_order_acquire))
  {
    expected = ScopeSlot::Ready;
    if (!slot.state.compare_exchange_strong(expected, ScopeSlot::Writing, std::memory_order_acquire))
      return;
  }

  const uint64_t sequence = mScopeSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
  std::copy(mLeft.begin(), mLeft.end(), slot.left.begin());
  std::copy(mRight.begin(), mRight.end(), slot.right.begin());
  slot.sequence.store(sequence, std::memory_order_relaxed);
  slot.state.store(ScopeSlot::Ready, std::memory_order_release);
}

int EC2Next::readScope(float* left, float* right, int maxFrames) const
{
  if (!left || !right || maxFrames <= 0) return 0;
  std::fill(left, left + maxFrames, 0.0f);
  std::fill(right, right + maxFrames, 0.0f);

  const uint64_t newest = mScopeSequence.load(std::memory_order_acquire);
  if (newest == 0) return maxFrames;
  const uint64_t blocks = static_cast<uint64_t>((maxFrames + kScopeBlock - 1) / kScopeBlock);

  for (ScopeSlot& slot : mScope)
  {
    int expected = ScopeSlot::Ready;
    if (!slot.state.compare_exchange_strong(expected, ScopeSlot::Reading, std::memory_order_acquire))
      continue;

    const uint64_t sequence = slot.sequence.load(std::memory_order_relaxed);
    const bool wanted = sequence <= newest && newest - sequence < blocks;
    if (wanted)
    {
      const int64_t start = static_cast<int64_t>(maxFrames) -
                            static_cast<int64_t>(newest - sequence + 1) * kScopeBlock;
      for (int i = 0; i < kScopeBlock; ++i)
      {
        const int64_t destination = start + i;
        if (destination >= 0 && destination < maxFrames)
        {
          left[destination] = slot.left[i];
          right[destination] = slot.right[i];
        }
      }
    }

    const uint64_t verify = slot.sequence.load(std::memory_order_relaxed);
    if (verify != sequence && wanted)
    {
      const int64_t start = static_cast<int64_t>(maxFrames) -
                            static_cast<int64_t>(newest - sequence + 1) * kScopeBlock;
      for (int i = 0; i < kScopeBlock; ++i)
      {
        const int64_t destination = start + i;
        if (destination >= 0 && destination < maxFrames)
          left[destination] = right[destination] = 0.0f;
      }
    }
    slot.state.store(ScopeSlot::Ready, std::memory_order_release);
  }
  return maxFrames;
}

void EC2Next::refill()
{
  std::fill(mLeft.begin(), mLeft.end(), 0.0f);
  std::fill(mRight.begin(), mRight.end(), 0.0f);
  mEngine.render(mLeft.data(), mRight.data(), kEngineBlock);
  captureScope();
  mRead = 0;
}

void EC2Next::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  if (!mEngine.isPrepared())
  {
    for (int c = 0; c < NOutChansConnected(); ++c)
      std::fill(outputs[c], outputs[c] + nFrames, 0.0);
    return;
  }

  const bool freeRun = GetParam(kFreeRun)->Bool();
  if (freeRun && !mFreeRunActive)
  {
    mEngine.noteOn(69, 1.0f, false);
    mFreeRunActive = true;
  }
  else if (!freeRun && mFreeRunActive)
  {
    mEngine.noteOff(69);
    mFreeRunActive = false;
  }

  const double gain = GetParam(kGain)->Value();
  const int channels = NOutChansConnected();

  for (int s = 0; s < nFrames; ++s)
  {
    if (mRead >= kEngineBlock) refill();

    outputs[0][s] = mLeft[mRead] * gain;
    if (channels > 1) outputs[1][s] = mRight[mRead] * gain;
    for (int c = 2; c < channels; ++c) outputs[c][s] = 0.0;

    ++mRead;
  }
}

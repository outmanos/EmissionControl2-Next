#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

struct Input
{
  const std::vector<uint8_t>* bytes;
  size_t position = 0;
};

int64_t CLAP_ABI Write(const clap_ostream_t* stream, const void* buffer, uint64_t size)
{
  auto* bytes = static_cast<std::vector<uint8_t>*>(stream->ctx);
  const auto* begin = static_cast<const uint8_t*>(buffer);
  bytes->insert(bytes->end(), begin, begin + size);
  return static_cast<int64_t>(size);
}

int64_t CLAP_ABI Read(const clap_istream_t* stream, void* buffer, uint64_t size)
{
  auto* input = static_cast<Input*>(stream->ctx);
  const size_t remaining = input->bytes->size() - input->position;
  const size_t count = static_cast<size_t>(std::min<uint64_t>(size, remaining));
  std::memcpy(buffer, input->bytes->data() + input->position, count);
  input->position += count;
  return static_cast<int64_t>(count);
}

const void* CLAP_ABI HostExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI HostCallback(const clap_host_t*) {}

double GetDouble(const std::vector<uint8_t>& bytes, size_t offset)
{
  double value = 0.0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

void SetDouble(std::vector<uint8_t>& bytes, size_t offset, double value)
{
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> Save(const clap_plugin_t* plugin, const clap_plugin_state_t* state)
{
  std::vector<uint8_t> bytes;
  clap_ostream_t stream{&bytes, Write};
  if (!state->save(plugin, &stream)) return {};
  return bytes;
}

uint32_t CLAP_ABI EmptySize(const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI EmptyGet(const clap_input_events_t*, uint32_t) { return nullptr; }
bool CLAP_ABI OutputPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

} // namespace

int main(int argc, char** argv)
{
  const std::string path = argc > 1 ? argv[1] : "build/out/EC2Next.clap";
  const std::wstring widePath(path.begin(), path.end());
  HMODULE library = LoadLibraryExW(widePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!library)
  {
    std::fprintf(stderr, "FAIL: LoadLibraryEx(%s) returned %lu\n", path.c_str(), GetLastError());
    return 2;
  }

  auto check = [](bool condition, const char* message) {
    if (!condition)
    {
      std::fprintf(stderr, "FAIL: %s\n", message);
      std::exit(1);
    }
  };

  const auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(library, "clap_entry"));
  check(entry && clap_version_is_compatible(entry->clap_version), "clap_entry export or version");
  check(entry->init(path.c_str()), "clap_entry.init");

  const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  check(factory && factory->get_plugin_count(factory) == 1, "plugin factory");
  const clap_plugin_descriptor_t* descriptor = factory->get_plugin_descriptor(factory, 0);
  check(descriptor && descriptor->id, "plugin descriptor");

  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.name = "EC2 state smoke test";
  host.vendor = "EmissionControl2-Next";
  host.url = "https://github.com/outmanos/EmissionControl2-Next";
  host.version = "1.0.0";
  host.get_extension = HostExtension;
  host.request_restart = HostCallback;
  host.request_process = HostCallback;
  host.request_callback = HostCallback;

  const clap_plugin_t* plugin = factory->create_plugin(factory, &host, descriptor->id);
  check(plugin && plugin->init(plugin), "create and init plugin");
  check(plugin->activate(plugin, 48000.0, 64, 512), "activate plugin");

  const auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  const auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
  check(params && state, "params and state extensions");
  check(params->count(plugin) == 71, "71 host parameters");

  clap_param_info_t grainRate{};
  check(params->get_info(plugin, 1, &grainRate), "Grain Rate parameter info");

  std::vector<uint8_t> bytes = Save(plugin, state);
  constexpr size_t kParamsBytes = 71 * sizeof(double);
  constexpr size_t kRanges = kParamsBytes + 2 * sizeof(uint32_t);
  constexpr size_t kUi = kRanges + 36 * 2 * sizeof(double);
  check(bytes.size() == kUi + sizeof(int32_t) + sizeof(double), "versioned state size");
  uint32_t magic = 0, version = 0;
  std::memcpy(&magic, bytes.data() + kParamsBytes, sizeof(magic));
  std::memcpy(&version, bytes.data() + kParamsBytes + sizeof(magic), sizeof(version));
  check(magic == 0x45433252 && version == 2, "range magic and version");
  check(GetDouble(bytes, kRanges + 7 * 16) == 1.0 &&
        GetDouble(bytes, kRanges + 7 * 16 + 8) == 1.0, "Sound File defaults to 1..1");

  SetDouble(bytes, sizeof(double), 50.0);
  SetDouble(bytes, kRanges, 2.0);
  SetDouble(bytes, kRanges + sizeof(double), 20.0);
  Input input{&bytes};
  clap_istream_t reader{&input, Read};
  check(state->load(plugin, &reader), "load modified range state");

  double value = 0.0;
  const bool gotClampedValue = params->get_value(plugin, grainRate.id, &value);
  const double normalized20 = (20.0 - 0.1) / 99.9;
  check(gotClampedValue && std::abs(value - normalized20) < 1e-9,
        "range edit clamps real value and normalized host value");

  bytes = Save(plugin, state);
  check(GetDouble(bytes, sizeof(double)) == 20.0, "clamped real value in saved state");
  check(GetDouble(bytes, kRanges) == 2.0 && GetDouble(bytes, kRanges + sizeof(double)) == 20.0,
        "edited range survives state round trip");
  check(GetDouble(bytes, kRanges + 7 * 16) == 1.0 &&
        GetDouble(bytes, kRanges + 7 * 16 + 8) == 1.0, "Sound File range survives state round trip");

  const int32_t studioSkin = 2;
  std::memcpy(bytes.data() + kUi, &studioSkin, sizeof(studioSkin));
  SetDouble(bytes, kUi + sizeof(studioSkin), 1.5);
  Input uiInput{&bytes};
  reader = {&uiInput, Read};
  check(state->load(plugin, &reader), "load skin and scale state");
  bytes = Save(plugin, state);
  int32_t loadedSkin = 0;
  std::memcpy(&loadedSkin, bytes.data() + kUi, sizeof(loadedSkin));
  check(loadedSkin == studioSkin && GetDouble(bytes, kUi + sizeof(loadedSkin)) == 1.5,
        "skin and scale survive state round trip");

  std::vector<uint8_t> version1(bytes.begin(), bytes.begin() + kUi);
  version = 1;
  std::memcpy(version1.data() + kParamsBytes + sizeof(magic), &version, sizeof(version));
  Input v1Input{&version1};
  reader = {&v1Input, Read};
  check(state->load(plugin, &reader), "version-1 range state remains compatible");

  std::vector<uint8_t> legacy(bytes.begin(), bytes.begin() + kParamsBytes);
  SetDouble(legacy, sizeof(double), 8.0);
  Input oldInput{&legacy};
  reader = {&oldInput, Read};
  check(state->load(plugin, &reader), "legacy 71-parameter state load");
  const double normalized8 = (8.0 - 0.1) / 99.9;
  check(params->get_value(plugin, grainRate.id, &value) && std::abs(value - normalized8) < 1e-9,
        "legacy real parameter value and normalized host value");

  bytes = Save(plugin, state);
  SetDouble(bytes, 70 * sizeof(double), 1.0);
  Input freeRunInput{&bytes};
  reader = {&freeRunInput, Read};
  check(state->load(plugin, &reader), "enable Free Run through state");

  clap_input_events_t inEvents{nullptr, EmptySize, EmptyGet};
  clap_output_events_t outEvents{nullptr, OutputPush};
  float left[512], right[512];
  float* channels[2] = {left, right};
  clap_audio_buffer_t output{};
  output.data32 = channels;
  output.channel_count = 2;
  clap_process_t process{};
  process.frames_count = 512;
  process.audio_outputs = &output;
  process.audio_outputs_count = 1;
  process.in_events = &inEvents;
  process.out_events = &outEvents;

  check(plugin->start_processing(plugin), "start processing");
  float peak = 0.0f;
  bool finite = true;
  for (int block = 0; block < 200; ++block)
  {
    std::fill(left, left + 512, 0.0f);
    std::fill(right, right + 512, 0.0f);
    process.steady_time = block * 512;
    check(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR, "audio process");
    for (int i = 0; i < 512; ++i)
    {
      finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
      peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    }
  }
  plugin->stop_processing(plugin);
  check(finite && peak > 1e-3f, "finite, non-silent Free Run audio through CLAP");
  std::printf("Free Run peak: %.6f\n", peak);

  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  FreeLibrary(library);
  std::printf("RESULT: PASS (CLAP entry, 71 parameters, ranges, legacy state, Free Run audio)\n");
  return 0;
}

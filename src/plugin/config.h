#pragma once

// iPlug2 plugin configuration for the stereo instrument and its EC2 editor.

#define PLUG_NAME "EC2Next"
#define PLUG_MFR "EmissionControl2-Next"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'E2Nx'
#define PLUG_MFR_ID 'EC2N'
#define PLUG_URL_STR "https://github.com/outmanos/EmissionControl2-Next"
#define PLUG_EMAIL_STR "none@example.com"
#define PLUG_COPYRIGHT_STR "GNU GPL v3.0"
#define PLUG_CLASS_NAME EC2Next

#define BUNDLE_NAME "EC2Next"
#define BUNDLE_MFR "EmissionControl2-Next"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "EC2Next"

// Instrument, stereo out, no audio input.
#define PLUG_CHANNEL_IO "0-2"
#define PLUG_LATENCY 0
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 1440
#define PLUG_HEIGHT 810
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1

#define ROBOTO_FN "Roboto-Regular.ttf"

#define VST3_SUBCATEGORY "Instrument"

#define CLAP_MANUAL_URL "https://github.com/outmanos/EmissionControl2-Next"
#define CLAP_SUPPORT_URL "https://github.com/outmanos/EmissionControl2-Next"
#define CLAP_DESCRIPTION "Granular synthesis instrument based on EmissionControl2"
#define CLAP_FEATURES "instrument", "synthesizer"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

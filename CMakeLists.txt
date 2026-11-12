# EmissionControl2-Next plugin build.
#
# Compiles the upstream EC2 engine from source at C++17 (iPlug2 requires it) and
# wraps it in a plugin. Unlike upstream's ecSource/CMakeLists.txt this builds no
# GUI, no RtAudio/RtMidi and no al_ext: the host provides audio, MIDI and the
# window, so only AlloLib core, Gamma and libsndfile are needed.
cmake_minimum_required(VERSION 3.14)
project(EmissionControl2Next VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMP0091 makes MSVC_RUNTIME_LIBRARY authoritative instead of being derived from
# the flags. One CRT for the whole build: iPlug2 defaults to the static runtime,
# so the engine and AlloLib are built the same way rather than fighting it. This
# must be set before any target is created.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(EC2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ecSource")
set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/iPlug2" CACHE PATH "iPlug2 root")
set(SNDFILE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build-deps/libsndfile" CACHE PATH "libsndfile prefix")

# SDKs live in this repository as pinned submodules, outside iPlug2's own tree.
set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vst3sdk" CACHE PATH "VST3 SDK root")
set(CLAP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/clap" CACHE PATH "CLAP SDK root")
set(CLAP_HELPERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/clap-helpers" CACHE PATH "CLAP helpers root")

# ----------------------------------------------------------------- engine deps
if(NOT SNDFILE_LIBRARY)
  find_library(SNDFILE_LIBRARY NAMES sndfile HINTS "${SNDFILE_ROOT}/lib")
endif()
if(NOT SNDFILE_INCLUDE_DIR)
  find_path(SNDFILE_INCLUDE_DIR sndfile.h HINTS "${SNDFILE_ROOT}/include")
endif()
if(NOT SNDFILE_LIBRARY OR NOT SNDFILE_INCLUDE_DIR)
  message(FATAL_ERROR "libsndfile not found. Run tools/bootstrap.ps1 or set SNDFILE_LIBRARY and SNDFILE_INCLUDE_DIR.")
endif()

# AlloLib declares an old cmake_minimum_required, which resets the policy scope
# inside its directory: CMP0091 becomes OLD again and CMAKE_MSVC_RUNTIME_LIBRARY
# is ignored there, so the runtime is derived from these flags instead. Rewriting
# them is what actually makes AlloLib and the engine use the static runtime.
if(MSVC)
  foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
    string(REPLACE "/MDd" "/MTd" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
    string(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
    string(REPLACE "/MDd" "/MTd" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
  endforeach()
endif()

add_subdirectory("${EC2_ROOT}/external/allolib" EXCLUDE_FROM_ALL)
add_subdirectory("${EC2_ROOT}/external/libsamplerate" EXCLUDE_FROM_ALL)

# ----------------------------------------------------------------------- engine
add_library(ec2_engine STATIC
  "${EC2_ROOT}/src/ecSynth.cpp"
  "${EC2_ROOT}/src/emissionControl.cpp"
  "${EC2_ROOT}/src/utility.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/EC2Adapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/EngineFacade.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/GladShim.cpp"
)

target_include_directories(ec2_engine PUBLIC
  "${EC2_ROOT}/include"
  "${EC2_ROOT}/external"
  "${EC2_ROOT}/external/allolib/include"
  "${EC2_ROOT}/external/allolib/external/Gamma"
  "${EC2_ROOT}/external/allolib/external/imgui"
  "${EC2_ROOT}/external/allolib/external/json/include"
  "${SNDFILE_INCLUDE_DIR}"
)

# Private: AlloLib stays inside the engine. The plugin sees only EngineFacade.h.
target_include_directories(ec2_engine PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/engine"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/instrument"
)

target_link_libraries(ec2_engine PUBLIC al Gamma imgui samplerate "${SNDFILE_LIBRARY}")

# AlloLib's graphics layer is compiled out: it links its own glad, which collides
# with the glad iPlug2 embeds for IGraphics. PUBLIC so the engine headers agree.
target_compile_definitions(al PUBLIC AL_NO_GRAPHICS)

# With the graphics layer compiled out, the engine no longer needs AlloLib's GL
# stack. Dropping it from al's link interface keeps AlloLib's glad out of the
# plugin, where it would collide with the one iPlug2 embeds.
foreach(_dep IN ITEMS glad)
  get_target_property(_iface al INTERFACE_LINK_LIBRARIES)
  if(_iface)
    list(REMOVE_ITEM _iface ${_dep})
    set_target_properties(al PROPERTIES INTERFACE_LINK_LIBRARIES "${_iface}")
  endif()
endforeach()

if(MSVC)
  # Two upstream defects, worked around per target because a global /FIchrono
  # also lands on C sources (WDL's win32_utf8.c) and breaks them:
  #   /FIchrono              al_Composition.cpp uses std::chrono without including it
  #   OSC_HOST_LITTLE_ENDIAN oscpack refuses to compile without an endianness define
  #   /wd4244                keeps the float-narrowing flood out of the log
  target_compile_options(al PRIVATE /FIchrono /wd4244)
  if(TARGET oscpack)
    target_compile_options(oscpack PRIVATE /DOSC_HOST_LITTLE_ENDIAN)
  endif()
  target_compile_options(ec2_engine PRIVATE /FIchrono /wd4244 /DOSC_HOST_LITTLE_ENDIAN)
endif()

# ----------------------------------------------------------------------- plugin
include("${IPLUG2_DIR}/iPlug2.cmake")
find_package(iPlug2 REQUIRED)

iplug_add_plugin(EC2Next
  SOURCES
    src/plugin/EC2Plugin.cpp
    src/plugin/EC2Plugin.h
    src/plugin/EC2Editor.cpp
    src/plugin/EC2Editor.h
    src/plugin/OscReceiver.cpp
  RESOURCES
    src/plugin/resources/fonts/Roboto-Regular.ttf
    src/plugin/resources/main.rc
  FORMATS
    VST3 CLAP APP
  UI
    IGRAPHICS
)

# iplug_add_plugin creates one target per format (EC2Next-vst3, EC2Next-clap)
# plus a shared interface target that all of them consume.
target_include_directories(_EC2Next-base INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/engine"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/resources"
)

# LINK_ONLY keeps AlloLib's include directories out of the plugin target, where they
# would collide with iPlug2's IGraphics glad headers. This is why the plugin
# talks to EngineFacade.h instead of including engine headers directly.
target_link_libraries(_EC2Next-base INTERFACE $<LINK_ONLY:ec2_engine>)

# sndfile.dll is bundled beside the plugin and delay-loaded, so the host's DLL
# search order cannot decide whether the plugin loads. See loadBundledSndfile().
if(MSVC)
  # sndfile.dll must sit beside each built plugin; loadBundledSndfile() loads it
  # from there. Derive it from the import library's location.
  get_filename_component(_sndfile_lib_dir "${SNDFILE_LIBRARY}" DIRECTORY)
  set(_sndfile_dll "${_sndfile_lib_dir}/../bin/sndfile.dll")
  foreach(target IN ITEMS EC2Next-vst3 EC2Next-clap)
    if(TARGET ${target})
      target_link_options(${target} PRIVATE "/DELAYLOAD:sndfile.dll")
      target_link_libraries(${target} PRIVATE delayimp)
      if(EXISTS "${_sndfile_dll}")
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${_sndfile_dll}" "$<TARGET_FILE_DIR:${target}>/sndfile.dll"
          COMMENT "Bundling sndfile.dll next to ${target}")
      endif()
    endif()
  endforeach()
endif()

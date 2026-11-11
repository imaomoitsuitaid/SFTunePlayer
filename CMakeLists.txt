cmake_minimum_required(VERSION 3.22)

# Version shown in the plugin and compared with GitHub Releases.
# Comes from the VERSION file; bump it and push to main to publish a new release.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SFTP_VERSION_FILE)
string(STRIP "${SFTP_VERSION_FILE}" SFTP_VERSION_FILE)
set(SFTP_VERSION "${SFTP_VERSION_FILE}" CACHE STRING "Plugin version (major.minor.patch)")
# GitHub repository used for update checks, e.g. "yourname/SFTunePlayer". Empty = no update checks.
set(SFTP_GITHUB_REPO "" CACHE STRING "owner/repo for update checks")

project(SFTunePlayer VERSION ${SFTP_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

# ---- JUCE: use -DJUCE_DIR=/path/to/JUCE, or download it automatically ----
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# Formats: VST3 everywhere, AU on macOS, plus a Standalone app for quick testing
set(SFTP_FORMATS VST3 Standalone CACHE STRING "Plugin formats to build")
if(APPLE)
    list(APPEND SFTP_FORMATS AU)
endif()

juce_add_plugin(SFTunePlayer
    COMPANY_NAME "SFTune"
    PRODUCT_NAME "SF Tune Player"
    PLUGIN_MANUFACTURER_CODE Sftn
    PLUGIN_CODE Sftp
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS ${SFTP_FORMATS})

target_sources(SFTunePlayer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SynthEngine.cpp
    Source/SF2Loader.cpp
    Source/SFZLoader.cpp
    Source/Tuning.cpp
    Source/UpdateChecker.cpp)

target_compile_definitions(SFTunePlayer PUBLIC
    SFTP_GITHUB_REPO="${SFTP_GITHUB_REPO}"
    SFTP_VERSION_STRING="${SFTP_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Minimal-dependency Linux builds (headless CI etc.) can turn these off
option(SFTP_MINIMAL_X11 "Linux build without ALSA and optional X11 extensions (CI / headless)" OFF)
if(SFTP_MINIMAL_X11)
    target_compile_definitions(SFTunePlayer PUBLIC
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_XRENDER=0 JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_CURL=0)
endif()

target_link_libraries(SFTunePlayer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline test tool (renders notes and checks pitch); not needed for the plugin ----
option(SFTP_BUILD_TESTS "Build the command-line engine test" OFF)
if(SFTP_BUILD_TESTS)
    juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
    target_sources(EngineTest PRIVATE
        Tests/EngineTest.cpp Source/UpdateChecker.cpp Source/SynthEngine.cpp Source/SF2Loader.cpp Source/SFZLoader.cpp Source/Tuning.cpp)
    target_include_directories(EngineTest PRIVATE Source)
    target_compile_definitions(EngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(EngineTest PRIVATE juce::juce_audio_formats juce::juce_audio_basics juce::juce_core juce::juce_events
                          PUBLIC juce::juce_recommended_config_flags)
endif()

if(SFTP_BUILD_TESTS)
    juce_add_console_app(HostTest PRODUCT_NAME "HostTest")
    target_sources(HostTest PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(HostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_XRENDER=0 JUCE_ALSA=0 JUCE_JACK=0)
    target_link_libraries(HostTest PRIVATE juce::juce_audio_processors PUBLIC juce::juce_recommended_config_flags)
endif()

if(SFTP_BUILD_TESTS)
    juce_add_console_app(EditorSnapshot PRODUCT_NAME "EditorSnapshot")
    target_sources(EditorSnapshot PRIVATE Tests/EditorSnapshot.cpp)
    target_include_directories(EditorSnapshot PRIVATE $<TARGET_PROPERTY:SFTunePlayer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(EditorSnapshot PRIVATE $<TARGET_PROPERTY:SFTunePlayer,COMPILE_DEFINITIONS>)
    target_link_libraries(EditorSnapshot PRIVATE SFTunePlayer)
endif()

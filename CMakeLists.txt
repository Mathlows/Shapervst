cmake_minimum_required(VERSION 3.22)

project(ShaperVST VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(SHAPER_BUILD_TESTS "Build the DSP unit tests" ON)
set(SHAPER_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (downloaded automatically if empty)")

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
if(SHAPER_JUCE_DIR)
    add_subdirectory(${SHAPER_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(SHAPER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SHAPER_FORMATS AU)
endif()

juce_add_plugin(ShaperVST
    COMPANY_NAME "Mathlows"
    BUNDLE_ID "com.mathlows.shapervst"
    PLUGIN_MANUFACTURER_CODE Mtlw
    PLUGIN_CODE Shp1
    PRODUCT_NAME "ShaperVST"
    FORMATS ${SHAPER_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE          # MIDI trigger (Retrigger / Envelope modes)
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_MusicEffect) # effect that accepts MIDI

juce_generate_juce_header(ShaperVST)

target_sources(ShaperVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/CurveEditor.cpp
    Source/dsp/ShapeCurve.cpp)

target_include_directories(ShaperVST PRIVATE Source)

target_compile_definitions(ShaperVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ShaperVST
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests (pure C++, no JUCE)
# ---------------------------------------------------------------------------
if(SHAPER_BUILD_TESTS)
    enable_testing()
    add_executable(ShaperTests tests/DspTests.cpp Source/dsp/ShapeCurve.cpp)
    target_include_directories(ShaperTests PRIVATE Source)
    add_test(NAME ShaperTests COMMAND ShaperTests)

    # Offline host test: links the real plugin code and drives it like a DAW would.
    add_executable(ShaperHostTest tests/HostTest.cpp)
    target_link_libraries(ShaperHostTest PRIVATE ShaperVST)
    target_include_directories(ShaperHostTest PRIVATE Source
        $<TARGET_PROPERTY:ShaperVST,INCLUDE_DIRECTORIES>)
    target_compile_definitions(ShaperHostTest PRIVATE
        $<TARGET_PROPERTY:ShaperVST,COMPILE_DEFINITIONS>)
    add_test(NAME ShaperHostTest COMMAND ShaperHostTest)
endif()

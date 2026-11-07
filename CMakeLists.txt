cmake_minimum_required(VERSION 3.22)

project(Lotus VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# Static MSVC runtime: users don't need the VC++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LOTUS_BUILD_PLUGIN "Build the plugin (requires JUCE)" ON)
option(LOTUS_BUILD_TESTS  "Build the headless engine tests" ON)
option(LOTUS_BUILD_CLAP   "Also build a CLAP plugin (via clap-juce-extensions)" ON)

# -----------------------------------------------------------------------------
# Engine (framework-free, shared by the plugin and the tests)

add_library(lotus_engine STATIC
    Source/dsp/SoundFont.cpp
    Source/dsp/Consoles.cpp
    Source/dsp/Synth.cpp
    Source/dsp/Effects.cpp
    Source/dsp/Codecs.cpp
    Source/dsp/VoiceRegistry.cpp)
target_include_directories(lotus_engine PUBLIC Source)
set_target_properties(lotus_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(lotus_engine PRIVATE /W4)
else()
    target_compile_options(lotus_engine PRIVATE -Wall -Wextra -Wshadow)
endif()

if(LOTUS_BUILD_TESTS)
    enable_testing()
    add_executable(lotus_tests tests/EngineTests.cpp)
    target_link_libraries(lotus_tests PRIVATE lotus_engine)
    add_test(NAME engine COMMAND lotus_tests)
endif()

# -----------------------------------------------------------------------------
# Plugin

if(LOTUS_BUILD_PLUGIN)
    include(FetchContent)
    # Use a local JUCE checkout with -DLOTUS_JUCE_DIR=/path/to/JUCE, otherwise fetch it.
    set(LOTUS_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
    if(LOTUS_JUCE_DIR)
        add_subdirectory(${LOTUS_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(LOTUS_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND LOTUS_FORMATS AU)
    elseif(UNIX)
        list(APPEND LOTUS_FORMATS LV2)
    endif()

    juce_add_plugin(Lotus
        PRODUCT_NAME "Lotus"
        COMPANY_NAME "Lotus"
        BUNDLE_ID "com.lotus.lotus"
        PLUGIN_MANUFACTURER_CODE Ltus
        PLUGIN_CODE Lts1
        FORMATS ${LOTUS_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Sampler
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        LV2URI "https://github.com/index573/Lotus"
        DESCRIPTION "A simple SoundFont player for video game music")

    target_sources(Lotus PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ui/LotusLookAndFeel.cpp
        Source/ui/Widgets.cpp)

    target_compile_definitions(Lotus PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Lotus
        PRIVATE
            lotus_engine
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # --- CLAP ---------------------------------------------------------------
    # JUCE 8 has no native CLAP support; clap-juce-extensions wraps the same
    # processor/editor as a .clap.
    if(LOTUS_BUILD_CLAP)
        set(LOTUS_CLAP_JUCE_EXTENSIONS_DIR "" CACHE PATH "Path to a local clap-juce-extensions checkout (optional)")
        if(LOTUS_CLAP_JUCE_EXTENSIONS_DIR)
            add_subdirectory(${LOTUS_CLAP_JUCE_EXTENSIONS_DIR} ${CMAKE_BINARY_DIR}/clap-juce-extensions EXCLUDE_FROM_ALL)
        else()
            FetchContent_Declare(clap-juce-extensions
                GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
                GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
                GIT_SUBMODULES_RECURSE TRUE)
            FetchContent_MakeAvailable(clap-juce-extensions)
        endif()

        clap_juce_extensions_plugin(TARGET Lotus
            CLAP_ID "com.lotus.lotus"
            CLAP_FEATURES instrument sampler synthesizer stereo)
    endif()
endif()

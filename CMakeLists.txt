cmake_minimum_required(VERSION 3.22)
project(ClassicStudioG VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is fetched at configure time, so nothing extra to install or commit.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # one universal build for Apple silicon and Intel
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    set(CSG_FORMATS AU VST3)       # AU is what Logic Pro loads
else()
    set(CSG_FORMATS VST3)          # Linux/Windows builds exist only for testing
endif()

juce_add_plugin(ClassicStudioG
    COMPANY_NAME "Minnkhai"
    PLUGIN_MANUFACTURER_CODE Mnkh
    PLUGIN_CODE Csgv
    FORMATS ${CSG_FORMATS}
    PRODUCT_NAME "Classic Studio G"
    BUNDLE_ID com.minnkhai.classicstudiog
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ClassicStudioG PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ClassicStudioG PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(ClassicStudioG PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

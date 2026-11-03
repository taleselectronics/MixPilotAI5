cmake_minimum_required(VERSION 3.22)
project(MixPilotAI5 VERSION 5.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(MixPilotAI5
 COMPANY_NAME "IndependentAudio" BUNDLE_ID "audio.independent.mixpilotai5"
 PLUGIN_MANUFACTURER_CODE InAu PLUGIN_CODE Mpa5
 FORMATS VST3 PRODUCT_NAME "MixPilot AI 5"
 IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
 EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE
 VST3_CATEGORIES Fx Dynamics EQ)
target_sources(MixPilotAI5 PRIVATE
 Source/PluginProcessor.cpp Source/PluginProcessor.h
 Source/PluginEditor.cpp Source/PluginEditor.h)
target_compile_definitions(MixPilotAI5 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MixPilotAI5 PRIVATE
 juce::juce_audio_utils juce::juce_dsp
 juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(WIN32)
 target_link_libraries(MixPilotAI5 PRIVATE winhttp)
endif()

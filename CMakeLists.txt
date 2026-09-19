cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(TheyThem VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

option(THEY_THEM_BUILD_PLUGIN "Build JUCE VST3 and standalone targets" ON)
option(THEY_THEM_TEST_EDITOR "Include editor smoke tests in CTest (requires a graphical session)" OFF)
set(THEY_THEM_JUCE_SOURCE_DIR "" CACHE PATH "Optional existing JUCE 9.0.2 source checkout")

add_library(TheyThemDsp STATIC Source/VocalEngine.cpp)
set_target_properties(TheyThemDsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(TheyThemDsp PUBLIC Source)
target_include_directories(TheyThemDsp SYSTEM PRIVATE
    ThirdParty/stftpitchshift)
target_compile_features(TheyThemDsp PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(TheyThemDsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(TheyThemDspTests tests/DspTests.cpp)
    target_link_libraries(TheyThemDspTests PRIVATE TheyThemDsp)
    add_test(NAME dsp COMMAND TheyThemDspTests)
    set_tests_properties(dsp PROPERTIES TIMEOUT 120)
endif()

if(THEY_THEM_BUILD_PLUGIN)
    if(THEY_THEM_JUCE_SOURCE_DIR)
        add_subdirectory("${THEY_THEM_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
    else()
        include(FetchContent)
        # JUCE 9.0.2. Pin both source revision and archive contents.
        FetchContent_Declare(JUCE
            URL https://codeload.github.com/juce-framework/JUCE/tar.gz/72782788ce18c2d4d760b28e0921d6ffc6431102
            URL_HASH SHA256=28ec8ae0626a4d1c6ca8c32ad13f95868b147779d4f3df4a7868f333a38e0551)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(TheyThem
        COMPANY_NAME "they-them"
        BUNDLE_ID "audio.theythem.voice"
        PLUGIN_MANUFACTURER_CODE ThTm
        PLUGIN_CODE Thtm
        PRODUCT_NAME "they-them"
        DESCRIPTION "Live pitch and formant vocal effect"
        FORMATS VST3 Standalone
        VST3_CATEGORIES Fx "Pitch Shift"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)

    target_sources(TheyThem PRIVATE
        Source/PluginProcessor.cpp Source/PluginProcessor.h
        Source/PluginEditor.cpp Source/PluginEditor.h)
    target_compile_definitions(TheyThem PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(TheyThem PRIVATE
        TheyThemDsp juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    if(BUILD_TESTING)
        add_executable(TheyThemPluginTests tests/PluginTests.cpp)
        add_executable(TheyThemEditorTests tests/EditorTests.cpp)
        foreach(test_target TheyThemPluginTests TheyThemEditorTests)
            target_link_libraries(${test_target} PRIVATE TheyThem)
            target_include_directories(${test_target} PRIVATE
                Source $<TARGET_PROPERTY:TheyThem,INCLUDE_DIRECTORIES>)
            target_compile_definitions(${test_target} PRIVATE
                $<TARGET_PROPERTY:TheyThem,COMPILE_DEFINITIONS>)
        endforeach()
        add_test(NAME plugin_contract COMMAND TheyThemPluginTests)
        set_tests_properties(plugin_contract PROPERTIES TIMEOUT 60)
        if(THEY_THEM_TEST_EDITOR)
            add_test(NAME editor COMMAND TheyThemEditorTests "${CMAKE_BINARY_DIR}/qa")
            set_tests_properties(editor PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()

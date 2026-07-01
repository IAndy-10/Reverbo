cmake_minimum_required(VERSION 3.22)

project(Reverbo VERSION 0.3.0)

# ============================================================
# C++ Standard
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# JUCE - FetchContent
# ============================================================

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

# ============================================================
# Plugin Definition
# ============================================================

juce_add_plugin(Reverbo
    COMPANY_NAME "YourCompany"
    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE Ap02
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "Reverbo"

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    NEEDS_JUCE_CORE TRUE
    NEEDS_JUCE_AUDIO_PROCESSORS TRUE
    NEEDS_JUCE_GUI_BASICS TRUE
    NEEDS_WEB_BROWSER TRUE

    COPY_PLUGIN_AFTER_BUILD TRUE

    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
)

juce_generate_juce_header(Reverbo)

# ============================================================
# Source Files
# ============================================================

target_sources(Reverbo PRIVATE
    # Plugin core
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/PluginEditor.h
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor.h
    Source/Plugin/WebviewBridge.h

    # Parameters
    Source/Parameters/ParameterIDs.h
    Source/Parameters/ParameterLayout.h

    # DSP - Header-only utilities
    Source/DSP/DiffusionNetwork/SmoothedValue.h
    Source/DSP/DiffusionNetwork/LFO.h
    Source/DSP/DiffusionNetwork/DelayLine.h
    Source/DSP/DiffusionNetwork/FeedbackMatrix.h
    Source/DSP/DiffusionNetwork/DiffusionStage.h
    Source/DSP/Decay/Freeze.h
    Source/DSP/Output/StereoWidener.h
    Source/DSP/Output/DryWetMixer.h

    # DSP - Filters
    Source/DSP/Input/InputFilter.h
    Source/DSP/Input/InputFilter.cpp
    Source/DSP/DiffusionNetwork/CrossoverFilter.h
    Source/DSP/DiffusionNetwork/CrossoverFilter.cpp

    # DSP - Delay-based
    Source/DSP/Input/Predelay.h
    Source/DSP/Input/Predelay.cpp
    Source/DSP/EarlyReflections/SpinModulator.h
    Source/DSP/EarlyReflections/SpinModulator.cpp

    # DSP - Early Reflections
    Source/DSP/EarlyReflections/EarlyReflections.h
    Source/DSP/EarlyReflections/EarlyReflections.cpp

    # DSP - Diffusion
    Source/DSP/DiffusionNetwork/DiffusionNetwork.h
    Source/DSP/DiffusionNetwork/DiffusionNetwork.cpp

    # DSP - FDN Core
    Source/DSP/Decay/FDNReverb.h
    Source/DSP/Decay/FDNReverb.cpp

    # DSP - Modulation
    Source/DSP/DiffusionNetwork/Chorus.h
    Source/DSP/DiffusionNetwork/Chorus.cpp
)

target_include_directories(Reverbo PRIVATE
    Source/Plugin
    Source/DSP
    Source/Parameters
)

# ============================================================
# Svelte Build Logic
# ============================================================

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist/index.html")
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist/index.html" "<html><body>Loading...</body></html>")
endif()

add_custom_target(BuildSvelteUI ALL
    COMMAND npm install
    COMMAND npm run build
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/WebUI"
    COMMENT "Building Svelte frontend..."
    VERBATIM
)

# ============================================================
# Embed Web Resources Into Binary
# ============================================================

juce_add_binary_data(Reverbo_Resources
    SOURCES
        WebUI/dist/index.html
)

add_dependencies(Reverbo_Resources BuildSvelteUI)

# ============================================================
# Compile Definitions & Linking
# ============================================================

target_compile_definitions(Reverbo PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

if(APPLE)
    target_compile_options(Reverbo PRIVATE
        -Wno-deprecated-declarations
    )
endif()

target_link_libraries(Reverbo PRIVATE
    Reverbo_Resources
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ============================================================
# Copy Resources to Bundle (Post-Build)
# ============================================================

macro(copy_resources_to_target target_name)
    if(TARGET ${target_name})
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist"
                "$<TARGET_FILE_DIR:${target_name}>/../Resources"
        )
    endif()
endmacro()

copy_resources_to_target(Reverbo)
copy_resources_to_target(Reverbo_Standalone)

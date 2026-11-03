cmake_minimum_required(VERSION 3.22)
project(Sandwave VERSION 1.0.0)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    set(SANDWAVE_FORMATS AU VST3)
else()
    set(SANDWAVE_FORMATS VST3)
endif()

add_subdirectory(JUCE)

juce_add_plugin(Sandwave
    COMPANY_NAME "Uranium Music"
    PLUGIN_MANUFACTURER_CODE Uran
    PLUGIN_CODE Swv1
    FORMATS ${SANDWAVE_FORMATS}
    PRODUCT_NAME "SANDWAVE"
    BUNDLE_ID com.uraniummusic.sandwave
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Analyzer)

set(SANDWAVE_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Plate.cpp Source/License.cpp Source/LicenseOverlay.cpp
    ThirdParty/monocypher/monocypher.c ThirdParty/monocypher/monocypher-ed25519.c)

target_sources(Sandwave PRIVATE ${SANDWAVE_SOURCES})
target_compile_features(Sandwave PRIVATE cxx_std_17)
if(APPLE)
    target_link_libraries(Sandwave PRIVATE "-framework IOKit")
endif()
target_compile_definitions(Sandwave PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Sandwave
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# JUCE adds moduleinfo.json after signing, which breaks the seal and stops hosts from loading the bundle
if(APPLE)
    add_custom_command(TARGET Sandwave_VST3 POST_BUILD
        COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:Sandwave_VST3>")
endif()

# Local test build that skips the license check; the release installers leave this off
option(SANDWAVE_DEV_UNLOCKED "Skip the license check (local testing only)" OFF)
if(SANDWAVE_DEV_UNLOCKED)
    message(WARNING "SANDWAVE_DEV_UNLOCKED is on: this build runs without a license. Do not ship it.")
    target_compile_definitions(Sandwave PRIVATE SANDWAVE_DEV_UNLOCKED=1)
endif()

# Headless tool that renders the editor to PNG and runs the license tests (for checking without a DAW)
juce_add_console_app(SandwaveSnapshot PRODUCT_NAME "SandwaveSnapshot")
set_target_properties(SandwaveSnapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(SandwaveSnapshot PRIVATE Tools/Snapshot.cpp ${SANDWAVE_SOURCES})
target_compile_features(SandwaveSnapshot PRIVATE cxx_std_17)
if(APPLE)
    target_link_libraries(SandwaveSnapshot PRIVATE "-framework IOKit")
endif()
target_compile_definitions(SandwaveSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JucePlugin_Name="SANDWAVE" JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_Manufacturer="Uranium Music" SANDWAVE_LICENSE_TESTING=1)
target_link_libraries(SandwaveSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)

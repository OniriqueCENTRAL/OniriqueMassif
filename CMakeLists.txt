cmake_minimum_required(VERSION 3.22)

project(OniriqueMassif VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary, and the oldest macOS the JUCE 9 runtime supports.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE is looked for next to this file first, then in the PitchBox checkout
# beside this one, so the plugins of the range can share a single copy.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(MASSIF_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
else()
    set(MASSIF_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../PitchBox/JUCE")
endif()

set(MASSIF_JUCE_DIR "${MASSIF_JUCE_DEFAULT}" CACHE PATH "Path to JUCE")

if(NOT EXISTS "${MASSIF_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${MASSIF_JUCE_DIR}.\n"
        "Run:  git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git")
endif()

add_subdirectory("${MASSIF_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The DSP, built on its own so the offline tests can link it without dragging
# in JUCE. Everything that decides how the plugin sounds lives here.
add_library(massif_core STATIC
    Source/dsp/Massif.cpp)

target_include_directories(massif_core PUBLIC Source)
set_target_properties(massif_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # MSVC's <cmath> only declares M_PI when asked. PUBLIC so the tests get it too.
    target_compile_definitions(massif_core PUBLIC _USE_MATH_DEFINES)
    target_compile_options(massif_core PRIVATE /W4)
else()
    target_compile_options(massif_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The plugin.
set(MASSIF_FORMATS VST3)

if(APPLE)
    list(APPEND MASSIF_FORMATS AU)
endif()

# The system VST3 folder on Windows is under Program Files, which a normal build
# cannot write to, so installing after the build is opt-in there.
if(WIN32)
    option(MASSIF_COPY_AFTER_BUILD "Install the plugin after building" OFF)
else()
    option(MASSIF_COPY_AFTER_BUILD "Install the plugin after building" ON)
endif()

juce_add_plugin(OniriqueMassif
    COMPANY_NAME "Onirique"
    BUNDLE_ID "com.onirique.massif"
    PLUGIN_MANUFACTURER_CODE Onrq
    PLUGIN_CODE Msif
    FORMATS ${MASSIF_FORMATS}
    # "Massif" alone is a common word: the file on disk carries the house's name too.
    PRODUCT_NAME "OniriqueMassif"
    PLUGIN_NAME "ONIRIQUE Massif"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx EQ
    COPY_PLUGIN_AFTER_BUILD ${MASSIF_COPY_AFTER_BUILD})

# The photographs the front is made of (tools/photo.cpp).
juce_add_binary_data(MassifAssets SOURCES
    Resources/photo/front-off.jpg
    Resources/photo/front-lit.jpg
    Resources/photo/knob.png
    Resources/photo/small.png
    Resources/photo/centre.png
    Resources/photo/toggle.png
    Resources/photo/lever.png)

target_sources(OniriqueMassif PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp)

# The licence every Onirique plugin carries: the folder is copied as it is from
# plugin-license/ in the ONIRIQUE CENTRAL repository, and is only ever changed there.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueMassif)

target_compile_definitions(OniriqueMassif PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# The descriptions are in French: the sources are UTF-8, and MSVC has to be told.
if(MSVC)
    target_compile_options(OniriqueMassif PRIVATE /utf-8)
endif()

target_link_libraries(OniriqueMassif
    PRIVATE
        massif_core
        MassifAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test: the whole audio path without a host.
enable_testing()

add_executable(test_massif tests/test_massif.cpp)
target_link_libraries(test_massif PRIVATE massif_core)
target_include_directories(test_massif PRIVATE tests)
add_test(NAME massif COMMAND test_massif)

# A development tool, not part of the product: renders the editor to a PNG
# without a host. Built only when asked for by name.
add_executable(massif_snapshot EXCLUDE_FROM_ALL tools/snapshot.cpp)

target_include_directories(massif_snapshot PRIVATE
    $<TARGET_PROPERTY:OniriqueMassif,INCLUDE_DIRECTORIES>)

target_compile_definitions(massif_snapshot PRIVATE
    $<TARGET_PROPERTY:OniriqueMassif,COMPILE_DEFINITIONS>)

if(MSVC)
    target_compile_options(massif_snapshot PRIVATE /utf-8)
endif()

target_link_libraries(massif_snapshot PRIVATE OniriqueMassif massif_core MassifAssets)

# A development tool, not part of the product: path-traces the photographs of
# the front and of every position of its controls into Resources/photo. It
# stands apart from the plugin, whose build needs those photographs.
juce_add_console_app(massif_photo PRODUCT_NAME "massif_photo")
set_target_properties(massif_photo PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(massif_photo PRIVATE tools/photo.cpp)

juce_add_binary_data(MassifPhotoData HEADER_NAME PhotoData.h NAMESPACE PhotoData SOURCES
    License/Fonts/HankenGrotesk600.ttf)
set_target_properties(MassifPhotoData PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_compile_definitions(massif_photo PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

if(MSVC)
    target_compile_options(massif_photo PRIVATE /utf-8 /fp:fast)
endif()

target_link_libraries(massif_photo PRIVATE MassifPhotoData juce::juce_gui_basics juce::juce_recommended_config_flags)

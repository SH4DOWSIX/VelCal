cmake_minimum_required(VERSION 3.22)

project(VelCal VERSION 0.0.3 LANGUAGES C CXX)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/velcal-version.txt" CONTENT "${PROJECT_VERSION}\n")

option(VELCAL_BUILD_TESTS "Build the VelCal core tests" ON)
option(VELCAL_BUILD_APP "Build the JUCE desktop application" ON)
option(VELCAL_BUILD_VST3 "Build the VelCal VST3 MIDI-processing instrument" ON)
option(VELCAL_INSTALLED "Use writable user storage for installed applications" OFF)
option(VELCAL_BUILD_AU "Build the macOS Audio Unit MIDI effect" ${APPLE})

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH
    "VelCal dependency storage (must remain on the D drive)" FORCE)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

if(VELCAL_BUILD_APP OR VELCAL_BUILD_VST3 OR VELCAL_BUILD_AU)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.3
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

add_library(velcal_core
    src/calibration.cpp
    src/press_collector.cpp
    src/profile.cpp
)

target_include_directories(velcal_core PUBLIC include)
target_compile_features(velcal_core PUBLIC cxx_std_17)
set_target_properties(velcal_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(velcal_core PUBLIC nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(velcal_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(velcal_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(velcal_analyze tools/analyze_capture.cpp)
target_link_libraries(velcal_analyze PRIVATE velcal_core)
target_compile_features(velcal_analyze PRIVATE cxx_std_17)
target_compile_definitions(velcal_analyze PRIVATE
    VELCAL_PROFILE_DIR="${CMAKE_SOURCE_DIR}/profiles")
if(MSVC)
    target_compile_options(velcal_analyze PRIVATE /W4 /permissive-)
endif()

if(VELCAL_BUILD_TESTS)
    enable_testing()
    add_executable(velcal_core_tests tests/calibration_tests.cpp)
    target_link_libraries(velcal_core_tests PRIVATE velcal_core)
    target_compile_features(velcal_core_tests PRIVATE cxx_std_17)
    add_test(NAME velcal_core_tests COMMAND velcal_core_tests)
endif()

if(WIN32)
    add_executable(velcal_capture tools/windows_capture.cpp)
    target_link_libraries(velcal_capture PRIVATE velcal_core winmm)
    target_compile_features(velcal_capture PRIVATE cxx_std_17)
    target_compile_definitions(velcal_capture PRIVATE
        VELCAL_CAPTURE_DIR="${CMAKE_SOURCE_DIR}/captures"
        VELCAL_PROFILE_DIR="${CMAKE_SOURCE_DIR}/profiles")
    if(MSVC)
        target_compile_options(velcal_capture PRIVATE /W4 /permissive-)
    endif()
endif()

if(VELCAL_BUILD_APP OR VELCAL_BUILD_VST3 OR VELCAL_BUILD_AU)
    set(VELCAL_ICON "${CMAKE_SOURCE_DIR}/resources/app-icon.png")
    juce_add_binary_data(velcal_icon
        HEADER_NAME VelCalIcon.h
        NAMESPACE VelCalIcon
        SOURCES "${VELCAL_ICON}"
    )
endif()

if(VELCAL_BUILD_APP)
    juce_add_gui_app(velcal_app
        PRODUCT_NAME "VelCal"
        COMPANY_NAME "VelCal"
        BUNDLE_ID "org.velcal.app"
        ICON_BIG "${VELCAL_ICON}"
        ICON_SMALL "${VELCAL_ICON}"
    )
    juce_generate_juce_header(velcal_app)
    target_sources(velcal_app PRIVATE
        app/Main.cpp
        app/MainComponent.cpp
        app/MainComponent.hpp
        app/Theme.hpp
        app/AppIcon.hpp
        app/MidiEngine.cpp
        app/MidiEngine.hpp
        app/PluginState.cpp
        app/PluginState.hpp
    )
    target_compile_features(velcal_app PRIVATE cxx_std_17)
    target_compile_definitions(velcal_app PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        VELCAL_DEFAULT_PROFILE_DIR="${CMAKE_SOURCE_DIR}/profiles"
        VELCAL_INSTALLED=$<BOOL:${VELCAL_INSTALLED}>
        VELCAL_ENABLE_UPDATE_CHECK=$<BOOL:${VELCAL_INSTALLED}>
        VELCAL_VERSION="${PROJECT_VERSION}"
    )
    target_link_libraries(velcal_app PRIVATE
        velcal_core
        velcal_icon
        juce::juce_audio_devices
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    if(VELCAL_BUILD_TESTS)
        juce_add_console_app(velcal_app_tests PRODUCT_NAME "VelCal app tests")
        juce_generate_juce_header(velcal_app_tests)
        target_sources(velcal_app_tests PRIVATE
            tests/app_tests.cpp app/MidiEngine.cpp app/MainComponent.cpp app/PluginState.cpp)
        target_include_directories(velcal_app_tests PRIVATE app)
        target_compile_features(velcal_app_tests PRIVATE cxx_std_17)
        target_compile_definitions(velcal_app_tests PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 VELCAL_INSTALLED=0 JUCE_MODAL_LOOPS_PERMITTED=1
            VELCAL_ENABLE_UPDATE_CHECK=0
            VELCAL_DEFAULT_PROFILE_DIR="${CMAKE_BINARY_DIR}/app-test-profiles"
            VELCAL_VERSION="${PROJECT_VERSION}")
        target_link_libraries(velcal_app_tests PRIVATE velcal_core velcal_icon
            juce::juce_audio_devices juce::juce_gui_extra
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME velcal_app_tests COMMAND velcal_app_tests)
        set_tests_properties(velcal_app_tests PROPERTIES TIMEOUT 30)
    endif()
    if(UNIX AND NOT APPLE)
        include(GNUInstallDirs)
        install(TARGETS velcal_app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
        install(FILES "${VELCAL_ICON}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/pixmaps"
            RENAME org.velcal.app.png)
        install(FILES resources/org.velcal.app.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    endif()
endif()

function(velcal_add_plugin target format midi_effect)
    if(midi_effect)
        set(is_synth FALSE)
        set(bundle_id org.velcal.au)
    else()
        set(is_synth TRUE)
        set(bundle_id org.velcal.vst3)
    endif()
    juce_add_plugin(${target}
        PRODUCT_NAME "VelCal"
        COMPANY_NAME "VelCal"
        BUNDLE_ID "${bundle_id}"
        PLUGIN_MANUFACTURER_CODE Vlcl
        PLUGIN_CODE VlC1
        FORMATS ${format}
        IS_SYNTH ${is_synth}
        IS_MIDI_EFFECT ${midi_effect}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Instrument Tools
        COPY_PLUGIN_AFTER_BUILD FALSE
    )
    juce_generate_juce_header(${target})
    target_sources(${target} PRIVATE app/PluginProcessor.cpp app/PluginProcessor.hpp
        app/PluginState.cpp app/PluginState.hpp app/MainComponent.cpp app/MainComponent.hpp
        app/Theme.hpp app/AppIcon.hpp app/MidiEngine.cpp app/MidiEngine.hpp)
    target_include_directories(${target} PUBLIC app)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
        VELCAL_AU_MIDI_EFFECT=$<BOOL:${midi_effect}>
        VELCAL_INSTALLED=$<BOOL:${VELCAL_INSTALLED}>
        VELCAL_ENABLE_UPDATE_CHECK=$<BOOL:${VELCAL_INSTALLED}>
        VELCAL_DEFAULT_PROFILE_DIR="${CMAKE_SOURCE_DIR}/profiles"
        VELCAL_VERSION="${PROJECT_VERSION}")
    target_link_libraries(${target} PUBLIC velcal_core velcal_icon juce::juce_audio_utils
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(VELCAL_BUILD_TESTS)
        juce_add_console_app(${target}_tests PRODUCT_NAME "VelCal plugin tests")
        juce_generate_juce_header(${target}_tests)
        target_sources(${target}_tests PRIVATE tests/plugin_tests.cpp)
        target_link_libraries(${target}_tests PRIVATE ${target})
        add_test(NAME ${target}_tests COMMAND ${target}_tests)
        set_tests_properties(${target}_tests PROPERTIES TIMEOUT 30)
    endif()
endfunction()

if(VELCAL_BUILD_VST3)
    velcal_add_plugin(velcal_plugin VST3 FALSE)
endif()
if(VELCAL_BUILD_AU)
    if(NOT APPLE)
        message(FATAL_ERROR "Audio Units are macOS-only")
    endif()
    velcal_add_plugin(velcal_au AU TRUE)
endif()
if(VELCAL_BUILD_TESTS AND (VELCAL_BUILD_APP OR VELCAL_BUILD_VST3 OR VELCAL_BUILD_AU))
    get_property(velcal_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${velcal_tests} PROPERTIES
        ENVIRONMENT "VELCAL_DATA_DIR=${CMAKE_BINARY_DIR}/test-user-data;VELCAL_DISABLE_UPDATE_CHECK=1")
endif()

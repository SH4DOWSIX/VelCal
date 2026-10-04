cmake_minimum_required(VERSION 3.22)

project(VelCal VERSION 0.1.0 LANGUAGES C CXX)

option(VELCAL_BUILD_TESTS "Build the VelCal core tests" ON)
option(VELCAL_BUILD_APP "Build the JUCE desktop application" ON)
option(VELCAL_PORTABLE "Store desktop app data beside its executable" OFF)

if(MSVC AND VELCAL_PORTABLE)
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

if(VELCAL_BUILD_APP)
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

if(VELCAL_BUILD_APP)
    set(VELCAL_ICON "${CMAKE_SOURCE_DIR}/resources/app-icon.png")
    juce_add_binary_data(velcal_icon
        HEADER_NAME VelCalIcon.h
        NAMESPACE VelCalIcon
        SOURCES "${VELCAL_ICON}"
    )
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
        app/MidiEngine.cpp
        app/MidiEngine.hpp
    )
    target_compile_features(velcal_app PRIVATE cxx_std_17)
    target_compile_definitions(velcal_app PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        VELCAL_DEFAULT_PROFILE_DIR="${CMAKE_SOURCE_DIR}/profiles"
        VELCAL_PORTABLE=$<BOOL:${VELCAL_PORTABLE}>
    )
    target_link_libraries(velcal_app PRIVATE
        velcal_core
        velcal_icon
        juce::juce_audio_devices
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
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

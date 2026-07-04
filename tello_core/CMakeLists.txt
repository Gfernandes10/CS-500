cmake_minimum_required(VERSION 3.16)
project(tello_core VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Include compiler options
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerOptions.cmake)

# Required FFmpeg stream backend
option(TELLO_ENABLE_FFMPEG "Enable FFmpeg video decoder backend when available" ON)
option(TELLO_REQUIRE_FFMPEG "Require FFmpeg backend and fail configure if missing" ON)
option(TELLO_ENABLE_OPENCV_VIEWER "Enable OpenCV rendering support for GUI/fallback tools" ON)
option(TELLO_ENABLE_QT_CONTROL_PANEL "Enable Qt-based tello_control_panel when Qt Widgets is available" ON)

find_package(PkgConfig QUIET)
if(TELLO_ENABLE_FFMPEG AND PkgConfig_FOUND)
    pkg_check_modules(AVFORMAT QUIET IMPORTED_TARGET libavformat)
    pkg_check_modules(AVCODEC QUIET IMPORTED_TARGET libavcodec)
    pkg_check_modules(AVUTIL QUIET IMPORTED_TARGET libavutil)
    pkg_check_modules(SWSCALE QUIET IMPORTED_TARGET libswscale)
endif()

set(TELLO_HAS_FFMPEG OFF)
if(TELLO_ENABLE_FFMPEG AND AVFORMAT_FOUND AND AVCODEC_FOUND AND AVUTIL_FOUND AND SWSCALE_FOUND)
    set(TELLO_HAS_FFMPEG ON)
    message(STATUS "FFmpeg stream backend enabled")
else()
    if(TELLO_REQUIRE_FFMPEG)
        message(FATAL_ERROR
            "FFmpeg stream backend is required but not available. "
            "Install libavformat/libavcodec/libavutil/libswscale development packages and pkg-config.")
    endif()
    message(STATUS "FFmpeg stream backend disabled by configuration")
endif()

# Library: tello_core
add_library(tello_core
    src/logger.cpp
    src/udp_socket.cpp
    src/command_executor.cpp
    src/tello_client.cpp
    src/state_parser.cpp
    src/state_receiver.cpp
    src/metrics.cpp
)
add_library(tello_core::tello_core ALIAS tello_core)

if(TELLO_HAS_FFMPEG)
    target_sources(tello_core PRIVATE src/video_stream_reader_ffmpeg.cpp)
    target_link_libraries(tello_core PRIVATE PkgConfig::AVFORMAT PkgConfig::AVCODEC PkgConfig::AVUTIL PkgConfig::SWSCALE)
endif()

target_include_directories(tello_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(tello_core PUBLIC Threads::Threads)

if(TELLO_HAS_FFMPEG)
    target_compile_definitions(tello_core PUBLIC TELLO_HAS_FFMPEG=1)
endif()

if(TELLO_ENABLE_OPENCV_VIEWER)
    find_package(OpenCV QUIET COMPONENTS core imgproc highgui)
    if(OpenCV_FOUND)
        message(STATUS "OpenCV viewer backend enabled")
    else()
        message(STATUS "OpenCV viewer backend disabled (OpenCV not found)")
    endif()
else()
    message(STATUS "OpenCV viewer backend disabled by configuration (TELLO_ENABLE_OPENCV_VIEWER=OFF)")
endif()

# CLI Application
add_executable(tello_cli
    apps/cli/main.cpp
)

target_link_libraries(tello_cli PRIVATE tello_core)

set(TELLO_QT_WIDGETS_TARGET "")
if(TELLO_ENABLE_QT_CONTROL_PANEL)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        set(TELLO_QT_WIDGETS_TARGET Qt6::Widgets)
        message(STATUS "Qt control panel enabled with Qt6")
    else()
        find_package(Qt5 QUIET COMPONENTS Widgets)
        if(Qt5_FOUND)
            set(TELLO_QT_WIDGETS_TARGET Qt5::Widgets)
            message(STATUS "Qt control panel enabled with Qt5")
        endif()
    endif()
endif()

if(TELLO_QT_WIDGETS_TARGET)
    add_executable(tello_control_panel
        apps/control_panel/main_qt.cpp
    )
    target_link_libraries(tello_control_panel PRIVATE tello_core ${TELLO_QT_WIDGETS_TARGET})
else()
    add_executable(tello_control_panel
        apps/control_panel/main.cpp
    )
    target_link_libraries(tello_control_panel PRIVATE tello_core)
    if(TELLO_ENABLE_OPENCV_VIEWER AND OpenCV_FOUND)
        target_link_libraries(tello_control_panel PRIVATE ${OpenCV_LIBS})
        target_compile_definitions(tello_control_panel PRIVATE TELLO_HAS_OPENCV=1)
    endif()
    message(STATUS "Qt Widgets not found; using OpenCV fallback for tello_control_panel")
endif()

# Enable testing
enable_testing()

option(ENABLE_HARDWARE_TESTS "Build and register hardware-dependent tests" OFF)

# Unit tests (offline)
add_executable(test_state_parser
    tests/test_state_parser.cpp
)
target_link_libraries(test_state_parser PRIVATE tello_core)
add_test(NAME unit_state_parser COMMAND test_state_parser)
set_tests_properties(unit_state_parser PROPERTIES LABELS "unit;offline")

add_executable(test_metrics_collector
    tests/test_metrics_collector.cpp
)
target_link_libraries(test_metrics_collector PRIVATE tello_core)
add_test(NAME unit_metrics_collector COMMAND test_metrics_collector)
set_tests_properties(unit_metrics_collector PROPERTIES LABELS "unit;offline")

# Hardware tests (require connected DJI Tello)
if(ENABLE_HARDWARE_TESTS)
    add_executable(test_hardware_command_channel
        tests/test_hardware_command_channel.cpp
    )
    target_link_libraries(test_hardware_command_channel PRIVATE tello_core)
    add_test(NAME hardware_command_channel COMMAND test_hardware_command_channel)
    set_tests_properties(hardware_command_channel PROPERTIES LABELS "hardware;integration")
endif()

# Install/export runtime artifacts without pulling the academic workspace into
# downstream consumers.
install(TARGETS tello_core tello_cli tello_control_panel
    EXPORT tello_coreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT tello_coreTargets
    FILE tello_coreTargets.cmake
    NAMESPACE tello_core::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tello_core
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tello_coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/tello_coreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tello_core
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/tello_coreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/tello_coreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/tello_coreConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tello_core
)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/control_profiles.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/tello_core
)

# Binary release packaging. GitHub Releases can publish the generated .tar.gz
# and .deb so ROS users can install the runtime without cloning this full repo.
set(CPACK_PACKAGE_NAME "tello_core")
set(CPACK_PACKAGE_VENDOR "CS500")
set(CPACK_PACKAGE_CONTACT "Gabriel Fernandes")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ runtime, CLI, and Qt control panel for DJI Tello")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Gabriel Fernandes")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libavformat-dev, libavcodec-dev, libavutil-dev, libswscale-dev, pkg-config"
)
include(CPack)

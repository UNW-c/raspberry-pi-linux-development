cmake_minimum_required(VERSION 3.20)

project(prts_terminal
    VERSION 0.1.0
    DESCRIPTION "PRTS terminal service - layered boot sequence for the Raspberry Pi"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(PRTS_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)

find_package(JPEG REQUIRED)

# ---------------------------------------------------------------------------
# Core library - every layer below app/ lives here so that tests can link it.
# ---------------------------------------------------------------------------
add_library(prts_core STATIC
    utils/ansi.cpp
    utils/text.cpp
    utils/utf8.cpp
    utils/logger.cpp
    driver/ansi_terminal.cpp
    driver/steady_clock_driver.cpp
    driver/linux_system_probe.cpp
    driver/mqtt_codec.cpp
    driver/tcp_mqtt_client.cpp
    driver/v4l2_camera.cpp
    driver/http_request.cpp
    driver/jpeg_codec.cpp
    service/config_service.cpp
    service/animation_service.cpp
    service/logo_art.cpp
    service/selfcheck_service.cpp
    service/session_state_service.cpp
    service/boot_sequence_service.cpp
    service/telemetry_service.cpp
    service/camera_service.cpp
    service/camera_web_service.cpp
    controller/boot_controller.cpp
    controller/mqtt_controller.cpp
    controller/camera_controller.cpp
    controller/web_controller.cpp
)

target_include_directories(prts_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(prts_core PUBLIC JPEG::JPEG)
target_compile_options(prts_core PRIVATE ${PRTS_WARNINGS})

# ---------------------------------------------------------------------------
# Application entry point
# ---------------------------------------------------------------------------
add_executable(prts-boot app/main.cpp)
target_link_libraries(prts-boot PRIVATE prts_core)
target_compile_options(prts-boot PRIVATE ${PRTS_WARNINGS})

add_executable(prts-mqtt app/mqtt_main.cpp)
target_link_libraries(prts-mqtt PRIVATE prts_core)
target_compile_options(prts-mqtt PRIVATE ${PRTS_WARNINGS})

add_executable(prts-camera app/camera_main.cpp)
target_link_libraries(prts-camera PRIVATE prts_core)
target_compile_options(prts-camera PRIVATE ${PRTS_WARNINGS})

add_executable(prts-web app/web_main.cpp)
target_link_libraries(prts-web PRIVATE prts_core)
target_compile_options(prts-web PRIVATE ${PRTS_WARNINGS})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(prts_tests
    test/main.cpp
    test/test_framework.cpp
    test/fakes.cpp
    test/test_text.cpp
    test/test_config.cpp
    test/test_selfcheck.cpp
    test/test_boot_sequence.cpp
    test/test_mqtt.cpp
    test/test_camera.cpp
    test/test_jpeg.cpp
    test/test_web.cpp
)
target_link_libraries(prts_tests PRIVATE prts_core)
target_compile_options(prts_tests PRIVATE ${PRTS_WARNINGS})
add_test(NAME prts_unit_tests COMMAND prts_tests)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS prts-boot prts-mqtt prts-camera prts-web RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/prts.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/prts)

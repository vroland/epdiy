# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.18)

# dependencies
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(screen_diag)

set(IDF_VER_SANE "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
if (IDF_VER_SANE VERSION_LESS "5.0.0")
    message(FATAL_ERROR "screen_diag requires at least ESP-IDF v5.0.0")
endif()

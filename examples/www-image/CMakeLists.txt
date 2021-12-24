cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS "../../src/")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(www-image_example)

# This is just to avoid that components/jpegdec includes Arduino.h
idf_build_set_property(COMPILE_OPTIONS "-D __LINUX__" APPEND)
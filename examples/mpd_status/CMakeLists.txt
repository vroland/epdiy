cmake_minimum_required(VERSION 3.16.0)
set(EXTRA_COMPONENT_DIRS "../../src/")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_get_property(idf_version IDF_VER)
string(REGEX MATCH "[0-9]\.[0-9]" idf_mm_version "${idf_version}")
if(NOT idf_mm_version)
    message(WARNING "Could not detect IDF version from \"${idf_version}\"")
elseif(idf_mm_version VERSION_LESS "5.0.0")
    # disable component manager to prevent ambiguity of some packages
    set(ENV{IDF_COMPONENT_MANAGER} 0)
    idf_build_set_property(IDF_COMPONENT_MANAGER "0")
endif()

project(mpd_status)

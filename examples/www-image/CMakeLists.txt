cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS "../../")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(www-image_example)

idf_component_get_property(jpegdec_lib bitbank2__jpegdec COMPONENT_LIB)
target_compile_options(
    ${jpegdec_lib}
    PRIVATE -Wno-error=pointer-to-int-cast -Wno-error=shift-negative-value
)

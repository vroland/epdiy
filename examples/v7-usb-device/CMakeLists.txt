cmake_minimum_required(VERSION 3.16.0)
set(EXTRA_COMPONENT_DIRS "../../"
    # Add also www-image components to avoid including them here again (JPEGDEC)
                         "../../examples/www-image/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(usb_cdc_example)

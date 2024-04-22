# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)

# Add newly added components to one of these lines:
# 1. Add here if the component is compatible with IDF >= v4.3
set(EXTRA_COMPONENT_DIRS "../../")

set(TEST_COMPONENTS "epdiy")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(epdiy_testrunner)

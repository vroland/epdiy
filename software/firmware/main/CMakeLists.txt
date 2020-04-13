#Warning !This code was automatically generated for projects
#without default 'CMakeLists.txt' file.

set(app_sources "EPD.c"
                "ed097oc4.c"
                "font.c"
                "i2s_data_bus.c"
                "main.c"
                "rmt_pulse.c")

idf_component_register(SRCS ${app_sources})

option(BUILD_SHARED_LIBS OFF)
option(SKIP_INSTALL_ALL ON)

add_subdirectory(lib/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)

target_link_libraries(${COMPONENT_LIB} PUBLIC zlib)

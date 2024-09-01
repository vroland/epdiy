set(app_sources "main.c")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
idf_component_register(SRCS ${app_sources} REQUIRES epdiy)

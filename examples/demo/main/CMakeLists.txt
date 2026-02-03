set(app_sources "main.c")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
idf_component_register(SRCS ${app_sources} REQUIRES epdiy)

# GCC 12+ (IDF 6.0) warns about bidi control chars in generated font headers
if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=bidi-chars=)
endif()

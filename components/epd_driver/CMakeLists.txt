set(app_sources "epd_driver.c"
                "ed097oc4.c"
                "font.c"
                "i2s_data_bus.c"
                "rmt_pulse.c"
				"epd_temperature.c")

idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include" REQUIRES esp_adc_cal)

option(BUILD_SHARED_LIBS OFF)
option(SKIP_INSTALL_ALL ON)

add_subdirectory(lib/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)

target_link_libraries(${COMPONENT_LIB} PUBLIC zlib)

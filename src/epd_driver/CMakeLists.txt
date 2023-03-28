
set(app_sources "epd_driver.c"
                "epd_board.c"
                "render.c"
                "render_lcd.c"
                "s3_lcd.c"
                "display_ops.c"
                "tps65185.c"
                "pca9555.c"
                "font.c"
                "lut.c"
                "board_specific.c"
                "builtin_waveforms.c"
                "line_queue.c"
                #"i2s_data_bus.c"
                "rmt_pulse.c"
                "highlevel.c"
                "epd_temperature.c"
                "board/epd_board_common.c"
                #"board/epd_board_lilygo_t5_47.c"
                #"board/epd_board_v2_v3.c"
                #"board/epd_board_v4.c"
                #"board/epd_board_v5.c"
                #"board/epd_board_v6.c"
                "board/epd_board_s3_prototype.c"
)


# Can also use IDF_VER for the full esp-idf version string but that is harder to parse. i.e. v4.1.1, v5.0-beta1, etc
if (${IDF_VERSION_MAJOR} GREATER 4)
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include" REQUIRES driver esp_timer esp_adc esp_lcd)
else()
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include" REQUIRES esp_adc_cal esp_timer)
endif()

set_source_files_properties("lut.c" PROPERTIES COMPILE_OPTIONS -mno-fix-esp32-psram-cache-issue)

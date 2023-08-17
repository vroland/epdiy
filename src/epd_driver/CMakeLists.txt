
set(app_sources "epd_driver.c"
                "render.c"
                "output_lcd/render_lcd.c"
                "output_lcd/lcd_driver.c"
                "output_i2s/render_i2s.c"
                "output_i2s/rmt_pulse.c"
                "output_i2s/i2s_data_bus.c"
                "output_common/lut.c"
                "output_common/line_queue.c"
                "output_common/render_context.c"
                "font.c"
                "board_specific.c"
                "builtin_waveforms.c"
                "highlevel.c"
                "board/epd_temperature.c"
                "board/tps65185.c"
                "board/pca9555.c"
                "board/epd_board.c"
                "board/epd_board_common.c"
                "board/epd_board_lilygo_t5_47.c"
                "board/epd_board_v2_v3.c"
                "board/epd_board_v4.c"
                "board/epd_board_v5.c"
                "board/epd_board_v6.c"
                "board/epd_board_s3_prototype.c"
)


# Can also use IDF_VER for the full esp-idf version string but that is harder to parse. i.e. v4.1.1, v5.0-beta1, etc
if (${IDF_VERSION_MAJOR} GREATER 4)
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include" REQUIRES driver esp_timer esp_adc esp_lcd)
else()
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include" REQUIRES esp_adc_cal esp_timer)
endif()

set_source_files_properties("lut.c" PROPERTIES COMPILE_OPTIONS -mno-fix-esp32-psram-cache-issue)

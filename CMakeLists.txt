
set(app_sources "src/epdiy.c"
                "src/render.c"
                "src/output_lcd/render_lcd.c"
                "src/output_lcd/lcd_driver.c"
                "src/output_i2s/render_i2s.c"
                "src/output_i2s/rmt_pulse.c"
                "src/output_i2s/i2s_data_bus.c"
                "src/output_common/lut.c"
                "src/output_common/lut.S"
                "src/output_common/line_queue.c"
                "src/output_common/render_context.c"
                "src/output_common/render_method.c"
                "src/font.c"
                "src/displays.c"
                "src/diff.S"
                "src/board_specific.c"
                "src/builtin_waveforms.c"
                "src/highlevel.c"
                "src/board/tps65185.c"
                "src/board/pca9555.c"
                "src/board/epd_board.c"
                "src/board/epd_board_common.c"
                "src/board/epd_board_lilygo_t5_47.c"
                "src/board/lilygo_board_s3.c"
                "src/board/epd_board_v2_v3.c"
                "src/board/epd_board_v4.c"
                "src/board/epd_board_v5.c"
                "src/board/epd_board_v6.c"
                "src/board/epd_board_v7.c"
                "src/board/epd_board_v7_raw.c"
)


# Can also use IDF_VER for the full esp-idf version string but that is harder to parse. i.e. v4.1.1, v5.0-beta1, etc
if (${IDF_VERSION_MAJOR} GREATER 4)
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "src/" REQUIRES driver esp_timer esp_adc esp_lcd)
else()
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "src/" REQUIRES esp_adc_cal esp_timer esp_lcd)
endif()

# formatting specifiers maybe incompatible between idf versions because of different int definitions
component_compile_options(-Wno-error=format= -Wno-format)
set_source_files_properties("src/output_common/lut.c" PROPERTIES COMPILE_OPTIONS -mno-fix-esp32-psram-cache-issue)

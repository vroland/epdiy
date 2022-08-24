
set(app_sources "epd_driver.c"
                "epd_board.c"
                "render.c"
                "display_ops.c"
                "tps65185.c"
                "pca9555.c"
                "font.c"
                "lut.c"
                "board_specific.c"
				"builtin_waveforms.c"
                "i2s_data_bus.c"
                "rmt_pulse.c"
				"highlevel.c"
                "epd_temperature.c"
                "board/epd_board_common.c"
                "board/epd_board_lilygo_t5_47.c"
                "board/epd_board_v2_v3.c"
                "board/epd_board_v4.c"
                "board/epd_board_v5.c"
                "board/epd_board_v6.c"
)


idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include" REQUIRES driver esp_timer esp_adc)

set_source_files_properties("lut.c" PROPERTIES COMPILE_OPTIONS -mno-fix-esp32-psram-cache-issue)

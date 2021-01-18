set(app_sources "src/private/epd_driver.c"
                "src/private/ed097oc4.c"
                "src/private/font.c"
                "src/private/i2s_data_bus.c"
                "src/private/rmt_pulse.c"
				"src/private/epd_temperature.c")

idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "src" REQUIRES esp_adc_cal)

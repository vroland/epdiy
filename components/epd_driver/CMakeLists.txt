set(app_sources "epd_driver.c"
                "render.c"
                "ed097oc4.c"
                "font.c"
                "i2s_data_bus.c"
                "rmt_pulse.c"
				"epd_temperature.c")

idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "include" REQUIRES esp_adc_cal)

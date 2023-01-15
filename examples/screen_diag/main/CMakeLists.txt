idf_component_register(SRCS "screen_diag.c"
                    INCLUDE_DIRS "."
                    REQUIRES epd_driver console nvs_flash fatfs)

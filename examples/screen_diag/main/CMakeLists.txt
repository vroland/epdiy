idf_component_register(SRCS "screen_diag.c" "epd.c" "commands/screen.c"
                    INCLUDE_DIRS "."
                    REQUIRES epd_driver console nvs_flash fatfs)

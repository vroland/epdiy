idf_component_register(SRCS "screen_diag.c" "epd.c" "commands.c" "commands/screen.c" "commands/graphics.c" "commands/tests.c"
                    INCLUDE_DIRS "." "res/fonts"
                    REQUIRES epd_driver console nvs_flash fatfs)

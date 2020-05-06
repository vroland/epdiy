set(app_sources "unicode.c" "main.c")

idf_component_register(SRCS ${app_sources} REQUIRES epd_driver)

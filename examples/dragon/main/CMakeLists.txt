set(app_sources "test.S" "main.c")

idf_component_register(SRCS ${app_sources} REQUIRES epdiy)

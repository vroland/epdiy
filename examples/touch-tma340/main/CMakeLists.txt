set(app_sources "touch-draw.c")

idf_component_register(SRCS ${app_sources} REQUIRES epdiy)

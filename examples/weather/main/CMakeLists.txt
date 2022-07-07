set(app_sources "weather.cpp")

idf_component_register(SRCS ${app_sources}
        REQUIRES epd_driver arduino)

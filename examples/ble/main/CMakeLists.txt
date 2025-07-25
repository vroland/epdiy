set(app_sources "receive-ble.cpp")

idf_component_register(
    SRCS ${app_sources} 
    REQUIRES epdiy 
    nvs_flash
    bt      # Bluetooth
    jpegdec # Jpeg decoder
    )

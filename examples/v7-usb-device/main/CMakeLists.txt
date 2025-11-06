# USB CDC examples - Only ESP32S3
# Note: In order to try this you need to have a USB connected directly to MCU like in v7 hardware
#       ESP_USB+ to GPIO19, ESP_USB- to GPIO20

# Select only ONE app_source:
set(app_sources 
# Dumb slow terminal example, type and write character in display
#"serial-note.c"
#"msc-sd/msc_main.cpp"
"utils_rand.cpp"
"msc-sd/msc_sleep.cpp"
)

set(requires epdiy 
    esp_tinyusb
    esp_timer
    fatfs 
    console
    # JpegDecoder
    jpegdec)

if(CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH)
    list(APPEND requires wear_levelling)
endif()

idf_component_register(
    SRCS "${app_sources}"
    INCLUDE_DIRS . "include"
    REQUIRES "${requires}"
)

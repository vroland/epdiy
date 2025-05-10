# USB CDC examples - Only ESP32S3
# Note: In order to try this you need to have a USB connected directly to MCU like in v7 hardware
#       ESP_USB+ to GPIO19, ESP_USB- to GPIO20

# Select only ONE app_source:



# Still in research  "serial-receive-file.c" IDEA: To make a port of rz (zmodem serial receive protocol to transfer image via serial)

set(app_sources 
# Dumb slow terminal example, type and write character in display

#"serial-note.c"
"msc-sd/msc_main.cpp"
#"msc-sd/msc_only_sd.cpp"
)

set(requires epdiy esp_tinyusb fatfs console
    # JpegDecoder
    jpegdec)

if(CONFIG_EXAMPLE_STORAGE_MEDIA_SPIFLASH)
    list(APPEND requires wear_levelling)
endif()

idf_component_register(
    SRCS "${app_sources}"
    INCLUDE_DIRS .
    REQUIRES "${requires}"
)

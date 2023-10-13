# USB CDC examples - Only ESP32S3
# Note: In order to try this you need to have a USB connected directly to MCU like in v7 hardware
#       ESP_USB+ to GPIO19, ESP_USB- to GPIO20

# Dumb slow terminal example, type and write character in display
set(app_sources "serial-note.c")

idf_component_register(SRCS ${app_sources} 
                       REQUIRES epdiy 
                                esp_tinyusb
                      )

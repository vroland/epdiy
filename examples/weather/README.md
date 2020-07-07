Weather display example
=======================================

This is port of cool project https://github.com/G6EJD/ESP32-e-Paper-Weather-Display to work with ED097OC4 Kindle display and this driver.

Building It
-----------

 - Girst you need to install Arduino esp-idf as a component https://github.com/espressif/arduino-esp32/blob/master/docs/esp-idf_component.md (the easiest way is to put it into components folder of ESP-IDF)
 - Put Arduino JSON https://github.com/bblanchon/ArduinoJson into components/arduino/
 - Dont forget to insert your Wi-Fi settings and openweathermap API key into owm_credentials.h
 - If you want to add more fonts, firmware may not fit into 1M and easiest way to fix it is to edit components/partition_table/partitions_singleapp.csv and change 1M to 2M

![weather image](weather.jpg)
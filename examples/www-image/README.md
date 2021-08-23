Download and render image example
=================================

After discussing the idea of collaborating adding an WiFi download and render example in the epdiy.slack.com 
we decided to also add a JPG decoding example suggested by @vroland.

  **jpg-render.c**
  Takes aprox. 1.5 to 2 seconds to download a 200Kb jpeg.

Additionally another second to decompress and render the image using EPDiy epd_draw_pixel()

**Note:** Statistics where taken with the 4.7" Lilygo display 960x540 pixels and may be significantly higher using bigger displays.

Building it
===========

Do not forget to update your WiFi credentials and point it to a proper URL that contains the image with the right format:

```c
// WiFi configuration
#define ESP_WIFI_SSID     "WIFI NAME"
#define ESP_WIFI_PASSWORD ""
// www URL of the JPG image. As default:
#define IMG_URL ("https://loremflickr.com/"STR(EPD_WIDTH)"/"STR(EPD_HEIGHT))
```

Note that as default an random image taken from loremflickr.com is used. You can use any URL that points to a valid Image, take care to use the right renderer (jpg or bmp), or you can also use the image-service [cale.es](https://cale.es) to create your own gallery.

If your epaper resolution is not listed, just send me an email, you can find it on my [profile page on Github](https://github.com/martinberlin).

Using HTTPS
===========

Using SSL requires a bit more effort if you need to verify the certificate. For example, getting the SSL cert from loremflickr.com needs to be extracted using this command:

    openssl s_client -showcerts -connect www.loremflickr.com:443 </dev/null

The CA root cert is the last cert given in the chain of certs.
To embed it in the app binary, the PEM file is named in the component.mk COMPONENT_EMBED_TXTFILES variable. This is already done for this random picture as an example.

**Important note about secure https**
Https is proved to work on stable ESP-IDF v4.2 branch. Using latest master I've always had resets and panic restarts, only working randomly. Maybe it's an issue will be fixed.

Also needs the main Stack to be bigger otherwise the embedTLS validation fails:
Just 1Kb makes it work: 
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4584

You can set this in **idf.py menuconfig**

     -> Component config -> ESP32-specific -> Main task stack size

Please be aware that in order to validate SSL certificates the ESP32 needs to be aware of the time. Setting the define VALIDATE_SSL_CERTIFICATE to true will make an additional SNTP server ping to do that. That takes between 1 or 2 seconds more.

Setting VALIDATE_SSL_CERTIFICATE to false also works skipping the .cert_pem in the esp_http_client_config_t struct. 


Happy to collaborate once again with this amazing project,

Martin Fasani, Berlin 20 Aug. 2021
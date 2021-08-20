Download and render image example
=================================

The BMP rendering example is taken from project Cale-idf and comes originally from a port made from GxEPD (v1)

After discussing the idea of collaborating adding an WiFi download and render example in the epdiy.slack.com 
we decided to also add a JPG decoding example suggested by @vroland.

Basically it comes in this two flavours:

  1. **bmp-render.c**   supports either 1 or 24 bits-depth bmp without compression.
  
  Takes aprox. 6 seconds to download a 1.5 MB 24 bits-depth bitmap. Features a fancy download bar (Attention: Makes things a bit slower)
  Does not need a img_buffer since it draws in streaming mode using **epd_draw_pixel**.

  2. **jpg-render.c**
  Takes aprox. 1.5 to 2 seconds to download a 200Kb jpeg.

Additionally another second to decompress it and render. 
Is faster than the bmp version and has no download progress bar. Uses more RAM since it needs both a src_buffer and a decoded_buffer.



**Note:** Statistics where taken with the 4.7" Lilygo display 960x540 pixels and may be significantly higher using bigger displays.

Building BMP or JPG version
===========================

Simply edit main/CMakeLists.txt and leave one of the app_sources uncommented:

```
# Select only one app_sources
#set(app_sources "bmp-render.c")
set(app_sources "jpg-render.c")
```

By default the jpg-render is the one that is built. 

As a second important configuration do not forget to update your WiFi credentials and point it to a proper URL that contains the image with the right format:

```c
// WiFi configuration
#define ESP_WIFI_SSID     "WIFI NAME"
#define ESP_WIFI_PASSWORD ""
// www URL of the JPG image
// Note: Only HTTP protocol supported (SSL secure URLs not supported yet)
#define IMG_URL "http://img.cale.es/jpg/fasani/5e636b0f39aac"
```

Note that as default an image gallery in Cale is used. You can use any URL that points to a valid Image, take care to use the right renderer (jpg or bmp), or you can also use the image-service [cale.es](https://cale.es) to create your own gallery.

If your epaper resolution is not listed, just send me an email, you can find it on my [profile page on Github](https://github.com/martinberlin).

Happy to collaborate once again with this amazing project,

Martin Fasani, Berlin 20 Aug. 2021

Tips & Tricks
=============

Image Ghosting
--------------

After multiple partial refreshes or drawing the same image multiple times,
a ghosting effect may occur.
That is, even after clearing the screen, the old image is still visible to a small degree.
In order to avoid that and increase the lifetime of your display,
you should first draw the inverse of an image before refreshing.

If the old image is stored in `framebuf` and was drawn with `draw_mode` `BLACK_ON_WHITE`,
the inverse image can be drawn with:
::
    
    epd_draw_image(epd_full_screen(), framebuf, WHITE_ON_WHITE);


Temperature Dependence
----------------------

The display refresh speed depends on the environmental temperature.
Thus, if your room temperature is significantly different from ~22°C, grayscale
accuracy might be affected.
This can be mitigated by using a different timing curve, but this would require calibrating the display timings at that temperature.
If you did this for some temperature other than room temperature, please submit a pull request!

Deep Sleep Current
------------------

Board Revision V5 is optimized for running from a battery thanks to its low deep sleep current consumption. 
In order to achieve the lowest possible deep sleep current, call
::

    epd_deinit()

before going to deep sleep. This will de-initialize the I2S peripheral used to drive the diplay and bring the pins used by epdiy to a low-power state.
You should be able to achieve a deep-sleep current of less than 13µA.
If your deep-sleep current is much higher, please check your attached peripherals.
With some modules, you have to isolate GPIO 12 before going to deep sleep:
::

    rtc_gpio_isolate(GPIO_NUM_12)




[![Documentation Status](https://readthedocs.org/projects/epdiy/badge/?version=latest)](https://epdiy.readthedocs.io/en/latest/?badge=latest) [![Matrix](https://img.shields.io/matrix/epdiy-general:matrix.vroland.de?label=Matrix%20Chat)](https://matrix.to/#/!GUXWriqsBKkWyXzsBK:matrix.vroland.de?via=matrix.vroland.de) [![JoinSlack](https://img.shields.io/badge/Join%20us-on%20Slack-blueviolet.svg)](https://join.slack.com/t/epdiy/shared_invite/zt-189eo7328-bs94cfB~eXPbLYAD1rKQcg)

EPDiy E-Paper Driver
=======================================

EPDiy is a driver board which talks to affordable E-Paper (or E-Ink) screens, which are usually sold as replacement screens for E-Book readers. Why are they interesting?

* Easy on the eyes and paper-like aesthetics
* No power consumption when not updating
* Sunlight-readable

Ready-made DIY modules for this size and with 4bpp (16 Grayscale) color support are currently quite expensive and / or slow.
The EPDiy controller can drive the bare display modules, e.g. from old e-Readers, which are available for 20$ (small) / 30$ (large) on ebay!
Additionally, since it is based on the ESP32S3 (V7) / ESP32 (V2-V6) microcontroller, it features WiFi and Bluetooth connectivity.

The EPDiy driver board targets a range of E-Paper displays, as shown in the table below.
As the driving method for all matrix-based E-ink displays is more or less the same, only the right connector and timings are needed.
The current V7 board has three different display connectors, other display will require an adapter board.

The controller is optimized for the use with LiPo batteries, featuring a LiPo charger and ultra-low deep sleep current.

This project supports a driver for the ESP-IDF and Arduino. For installation instructions, please refer to the [documentation](https://epdiy.readthedocs.io/en/latest/getting_started.html#getting-your-board).
Note that for epdiy V7, update speeds are significantly lower when using the Arduino IDE, because it does not allow to change
the sub-optimal cache configuration.

Building It
-----------

If you want to build a board right now, there are two possible routes:

 - Use the new v6 PCB (`hardware/epdiy-v7/gerbers.zip`). 
   It is the fastest and most flexible controller revision, featuring support
   for high-resolution 16-bit parallel displays.
   The BOM is available at (`hardware/epaper-breakout/BOM.csv`).
   Positioning files for SMT assembly are available at (`hardware/epaper-breakout/gerbers/epaper-breakout-top-pos.csv`). 
   Please double check the part positioning and Rotation with your assembly service!
   Additionally, you may want to consider part alternatives for connectors depending on 
   availability and price.
   More information on the order process and where to find parts is in the [documentation](https://epdiy.readthedocs.io/en/latest/getting_started.html#getting-your-board).
 
 
 - Use the old V6 PCB (`hardware/epaper-breakout/gerbers_v6.zip`).  
   You may want to consider this option if you require connectors that are missing on V7.
   Keep in mind that display updates will be slower than V7.

Gettings Started
----------------

For instructions and more information visit the [documentation](https://epdiy.readthedocs.io/en/latest/getting_started.html)!

Join the Discussion
----------------

 - [![Matrix](https://img.shields.io/matrix/epdiy-general:matrix.vroland.de?label=Join%20Matrix)](https://matrix.to/#/!GUXWriqsBKkWyXzsBK:matrix.vroland.de?via=matrix.vroland.de) Matrix Community: +epdiy:matrix.vroland.de
 - Slack: See badge

Displays
--------

|Name      |Size |Resolution|Compatible|Connector|Pin count|Compatible since pcb version|Notes
| --:      | --: | --:       | --:         | --:                  | --: | --: | --: |
| ED060SC4 | 6"  | 800 x 600 | yes, tested | FH26W-39S-0.3SHW(60) | 39  | v2  |     |
|ED097OC44 | 6"  | 800 x 600 | y|9.7"|1200 x 825|yes, tested|XF2M-3315-1A|33|V2|Cheap, inferior contrast
|ED097TC24 | 6"  | 800 x 600 | y|9.7"|1200 x 825|yes, tested|XF2M-3315-1A|33|V2|Slightly higher price, better contrast
|ED097OC14 | 6"  | 800 x 600 | y|9.7"|1200 x 825|yes (should work)|XF2M-3315-1A|33|V2|Cheap, inferior performance
|ED047TC14 | 6"  | 800 x 600 | y|4.7"|960 x 540|yes, tested|40-pin|40|LILYGO 4.7" EPD|Supported only by 4.7" e-paper board by LILYGO
|ED052TC24 | 6"  | 800 x 600 | y|5.2"|960 x 540|yes, tested|40-pin|40|LILYGO 4.7" EPD|Supported only by 4.7" e-paper board by LILYGO
| ED050SC5 | 5" | 600 x 800 | yes, tested       | THD0510-33CL-GF | 33 | v5 |
| ED050SC3 | 5" | 600 x 800 | yes (should work) | THD0510-33CL-GF | 33 | v5 |
|ED133UT2  |13.3"|1600 x 1200|yes, tested|adapter board|39|V2|Adapter Board required, also PENG133D
|ED060XC3  |6"   |758 x 1024|yes, tested|THD0515-34CL-SN|34|V5|Cheapest, good contrast and resolution
|ED060XD4  |6"   |758 x 1024|yes, tested|THD0515-34CL-SN|34|V5|
|ED060XC5  |6"   |758 x 1024|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|
|ED060XD6  |6"   |758 x 1024|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|
|ED060XH2  |6"   |758 x 1024|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|
|ED060XC9  |6"   |758 x 1024|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|
|ED060KD1  |6"   |1072 x 1448|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|
|ED060KC1  |6"   |1072 x 1448|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|
|ED060SCF  |6"   |600 x 800|yes, tested|THD0515-34CL-SN|34|V5|Different flex cable shape
|ED060SCN  |6"   |600 x 800|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|Different flex cable shape
|ED060SCP  |6"   |600 x 800|yes (should work as ED060XC3)|THD0515-34CL-SN|34|V5|Different flex cable shape
| ED060SC7 | 6"  | 600 x 800 | yes (should work) | AXT334124 | 34 | v5 |connector dropped in v6
| ED060SCG | 6"  | 600 x 800 | yes (should work) | AXT334124 | 34 | v5 |connector dropped in v6
| ED060SCE | 6"  | 600 x 800 | yes (should work) | AXT334124 | 34 | v5 |connector dropped in v6
| ED060SCM | 6"  | 600 x 800 | yes (should work) | AXT334124 | 34 | v5 |connector dropped in v6
| ED060SCT | 6"  | 600 x 800 | yes, tested       | AXT334124 | 34 | v5 |connector dropped in v6
| ED078KC1 | 6"  | 1872 x 1404 | yes, tested     | FH12-40S-0.5SH | 40 | v7 | 16 data lines


Troubleshooting
---------------

The following list is compiled from past experiences and GitHub issues:
 * **The displayed image displays only part of the image.** Make sure to select the correct display type as described [here](https://epdiy.readthedocs.io/en/latest/getting_started.html#selecting-a-display-type).
 * **The existing image fades / darkens when updating a partial screen region.** Make sure the VCOM voltage is [calibrated](https://epdiy.readthedocs.io/en/latest/getting_started.html#calibrate-vcom) for your specific display.
 * **The second third of the image is replaced with the last third.** This seems to be a timing issue we could not yet quite figure out the reason for. For a workarround or suggestions please [join the discussion](https://github.com/vroland/epdiy/issues/15).
 * **The ESP does not boot correctly when external periperals are connected.** Make sure not to pull GPIO12 high during boot, as it is a strapping pin internal voltage selection (https://github.com/vroland/epdiy/issues/17).

LilyGo Boards
---------------
There are several differences with these boards.
One particular one is the way the LilyGo handles power to the display the official lilygo code has two states.
This is now handled in epdiy in a different way to the lilygo code.
**epd_poweroff()** completely turns the power off to the display and the other peripherals of the lilygo.
The new function **epd_powerdown()** keeps the peripherals on (this allows the touch functions to continue to work). 
**epd_poweroff() should allways be called before sleeping the system**
You can still use touch to wake the screen with the following.
In Arduino it works like this.
`epd_poweroff();`

 `epd_deinit();`

 `esp_sleep_enable_ext1_wakeup(GPIO_SEL_13, ESP_EXT1_WAKEUP_ANY_HIGH);`

 `esp_deep_sleep_start();`

More on E-Paper Displays
------------------------

 * List of E-Paper devices and their screens: https://e-ink-reader.ru/chg_dsp_en.php (thanks, @mcer12)
 * Reverse engineering of display pins, possible starting point for alternative drivers: https://linux-sunxi.org/PocketBook_Touch_Lux_3 (thanks, @mcer12)
 * Reverse engineering and explanation on driving EPDs: http://essentialscrap.com/eink/index.html
 * Earlier take on driving 6" EPDs with an ESP32, basis of original power circuit: http://spritesmods.com/?art=einkdisplay&page=1
 * Related Hackaday projects: https://hackaday.io/project/21607 https://hackaday.io/project/11537

If you know any other useful resources or have suggestions, please comment on #31!

Hackaday Project
----------------

For more details, see the project page on Hackaday: https://hackaday.io/project/168193-epdiy-976-e-paper-controller

![demo image](doc/source/img/demo.jpg)
![board front](doc/source/img/v6.jpg)

Licenses
--------

The weather example is Copyright (c) David Bird 2018 (except for minor modifications). Please refer to `examples/weather/README.md` for details.

The board and schematic are licensed under a <a rel="license" href="https://creativecommons.org/licenses/by-sa/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-sa/4.0/80x15.png" /></a> <a rel="license" href="https://creativecommons.org/licenses/by-sa/4.0/">Creative Commons Attribution-ShareAlike 4.0 International License</a>.

Firmware and remaining examples are licensed under the terms of the GNU Lesser GPL version 3.
Utilities are licensed under the terms of the MIT license.

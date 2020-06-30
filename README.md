[![Documentation Status](https://readthedocs.org/projects/epdiy/badge/?version=latest)](https://epdiy.readthedocs.io/en/latest/?badge=latest)

EPDiy E-Paper Driver
=======================================

EPDiy is a driver board which talks to affordable E-Paper (or E-Ink) screens, which are usually sold as replacement screens for E-Book readers. Why are they interesting?

* Easy on the eyes and paper-like aesthetics
* No power consumption when not updating
* Sunlight-readable

Ready-made DIY modules for this size and with 4bpp (16 Grayscale) color support are currently quite expensive. This project uses Kindle replacement screens, which are available for 20$ (small) / 30$ (large) on ebay!

The EPDiy driver board targets multiple E-Paper displays. As the driving method for all matrix-based E-ink displays seems to be more or less the same, only the right connector and timings are needed. The EPDiy PCB features a 33pin and a 39pin connector, which allow to drive the following display types: ED097OC4, ED060SC4, ED097TC2

Displays
--------

| Display  | Size | Resolution | Grey levels | Connector | Bom-Part
| --: | --: | --: | --: | --: | --: |
| ED097OC4  | 9.7" | 1200 x 825 | 16 | 33 | XF2M-3315-1A |
| ED060SC4  | 6" | 800 x 600  | 16 | 39 | FH26W-39S-0.3SHW(60) |
| ED097TC2  | 9.7" | 1200 x 825  | 16 | 33 | XF2M-3315-1A |

Building It
-----------

If you want to build a board right now, there are two possible routes:
 - Use the current master-branch PCB (`hardware/epaper-breakout/gerbers`), contributed by @sebastius.
   They, offer some improvements over the original v2 PCB, as described here: https://github.com/vroland/epdiy/issues/3.
   Although this version *should* work, it was not physically built yet. (if you did, please contact me!)
 - Use the original v2 PCB (`hardware/epaper-breakout/gerbers_v2_rev0.zip`). 
   However, you might run into issue #3 with some selections of parts.

Gettings Started
----------------

For instructions and more information visit the [documentation](https://epdiy.readthedocs.io/en/latest/getting_started.html)!

Hackaday Project
----------------

For more details, see the project page on Hackaday: https://hackaday.io/project/168193-epdiy-976-e-paper-controller

![demo image](doc/source/img/demo.jpg)
![board front](doc/source/img/board_p1.jpg)
![board back](doc/source/img/board_p2.jpg)

Licenses
--------

The board and schematic are licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/80x15.png" /></a> <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.

Firmware and utilities are licensed unter the terms of the MIT license.

PCB tester
==========

This PCB tester is just a simple script that will iterate first the data lines (0 to 15) in a sequencial mode. 
Then it will light all of them at the same time and turn it down.
And then as latest step will do the same with the signals.

That way you can discover in a simple way if there is any bridge in your soldering, either in MCU or in the FPC connectors, without the need to connect a display.

The PCB you can find in the [vroland.github.io/epdiy-hardware](https://vroland.github.io/epdiy-hardware) open source files. But you can also do it yourself with a 40 pin expander breakout and soldering the Leds+ Resistances manually.

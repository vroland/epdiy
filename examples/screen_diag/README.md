# Screen Diagnostics
This example app implements a simple shell to allow you to tinker with the display. You have access to all drawing functions from the _epd driver_, as well as system information, etc.

There are also pre-programmed algorithms (e.g. `render_stairs`, `render_grid`) which can be used to find pixel errors or display incompatibilities.

The `screen_diag` examples requires ESP-IDF v5.x or newer.

## Setup
Don't forget to set your display type for the  _epd driver_ in `menuconfig`!

First you need to flash the firmware:
```sh
idf.py flash
```

After that, you can enter the shell environment with:
```sh
idf.py monitor
```

Please note the [known issues](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-monitor.html#known-issues-with-idf-monitor) of the IDF Monitor for limitations.

## Usage
You can get a full list (see below) of all available commands with: `help`.

```
system_restart
  Restarts the system.

free_heap_size
  Returns the free heap size.

dump_heaps_info  [<caps>]
  Dumps heap information of all heaps matching the capability.
        <caps>  Heap caps to print. Default: MALLOC_CAP_DEFAULT

dump_tasks
  Dumps all tasks with their names, current state and stack usage.

chip_info
  Dumps chip information.

firmware_info
  Dumps information about the ESP-IDF and the firmware.

get_time
  Returns the time in microseconds since boot.

get_mac  [<interface>]
  Returns the MAC address for the given interface or the pre-programmed base
  address.
   <interface>  Either "wifi_station", "wifi_ap", "bluetooth" or "ethernet"

get_rotation
  Get current screen rotation.

set_rotation  <rotation> [--inverted]
  Changes screen rotation.
    <rotation>  screen rotation: "horizontal" or "portrait"
    --inverted

get_width
  Print screen width.

get_height
  Print screen height.

get_pixel  <posx> <posy>
  Get pixel color in front buffer.
        <posx>  x position
        <posy>  y position

set_pixel  <posx> <posy> [<color>]
  Set pixel color in front buffer.
        <posx>  x position
        <posy>  y position
       <color>  color. default value: 0 (0x00)

clear_screen
  Clear the entire screen and reset the front buffer to white.

full_clear_screen
  Same as clear_screen, but also tries to get rid of any artifacts by cycling
  through colors on the screen.

get_temp
  Returns the ambient temperature.

power_on
  Turns on the power of the display.

power_off
  Turns off the power of the display.

draw_hline  <x> <y> <len> [<color>]
  Draw horizontal line.
           <x>  start x position
           <y>  start y position
         <len>  length of the line
       <color>  default value: 0x00

draw_vline  <x> <y> <len> [<color>]
  Draw vertical line.
           <x>  start x position
           <y>  start y position
         <len>  length of the line
       <color>  default value: 0x00

draw_line  <start_x> <start_y> <end_x> <end_y> [<color>]
  Draw line between two points.
     <start_x>  start x position
     <start_y>  start y position
       <end_x>  end x position
       <end_y>  end y position
       <color>  default value: 0x00

draw_rect  <x> <y> <width> <height> [<color>]
  Draw a rectangle.
           <x>  top left x position
           <y>  top left y position
       <width>  square width
      <height>  square height
       <color>  default value: 0x00

fill_rect  <x> <y> <width> <height> [<color>]
  Draw a filled rectangle.
           <x>  top left x position
           <y>  top left y position
       <width>  square width
      <height>  square height
       <color>  default value: 0x00

draw_circle  <center_x> <center_y> <radius> [<color>]
  Draw a circle.
    <center_x>  center x position
    <center_y>  center y position
      <radius>  circle radius
       <color>  default value: 0x00

fill_circle  <center_x> <center_y> <radius> [<color>]
  Draw a filled circle.
    <center_x>  center x position
    <center_y>  center y position
      <radius>  circle radius
       <color>  default value: 0x00

draw_triangle  <x0> <y0> <x1> <y1> <x0> <y0> [<color>]
  Draw a triangle from three different points.
          <x0>  first edge x position
          <y0>  first edge y position
          <x1>  second edge x position
          <y1>  second edge y position
          <x0>  third edge x position
          <y0>  third edge y position
       <color>  default value: 0x00

fill_triangle  <x0> <y0> <x1> <y1> <x0> <y0> [<color>]
  Draw a filled triangle from three different points.
          <x0>  first edge x position
          <y0>  first edge y position
          <x1>  second edge x position
          <y1>  second edge y position
          <x0>  third edge x position
          <y0>  third edge y position
       <color>  default value: 0x00

write_text  [-s] <x> <y> [<color>] <msg>
  Write text message to the screen using the sans-serif font by default.
           <x>  x position
           <y>  y position
       <color>  default value: 0x00
   -s, --serif  Use serif font rather than sans-serif.
         <msg>  Text to be printed.

render_stairs  [<slope>] [<width>] [<color>]
  Render multiple diagonal lines across the screen.
       <slope>  angle by which each diagonal line is drawn. default value: 3
       <width>  thickness of each diagonal line. default value: 100
       <color>  default value: 0x00

render_grid  [<gutter>] [<color>]
  Renders a grid across the whole screen. At a certain gutter size, cell info
  will be printed as well.
      <gutter>  default value: 75
       <color>  default value: 0x00

help
  Print the list of registered commands
```

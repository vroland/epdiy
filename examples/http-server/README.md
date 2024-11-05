# HTTP server

Runs an HTTP server that will draw images on the screen.  
Useful for setting up a small digital frame that can be remotely controlled.  

## Config
1. In `main/server.h`, edit `WIFI_SSID` and `WIFI_PASSWORD` to match your wifi config
2. In `main/main.c`, edit `n_epd_setup` to refer to the right EPD screen

## Running
flash (`idf.py flash`), then connect the EPDiy to a power source (computer is fine).  
The endpoints are:
1. `GET /`, prints the screen temp / height / width as headers
2. `POST /clear`, clears the screen
3. `POST /draw`, expects:
   1. a body that is a binary stream already encoded to EPDiy's standards (like the one in `dragon.h`).
   2. Headers `width`, `height`
   3. Optional headers `x`,`y` (default to 0)
   4. Optional header `clear`, if set to nonzero integer will force-clear the screen before drawing

## Helper script
`send_image.py` is a friendlier client.
```bash
$ ./send_image.py ESP_IP info
EpdInfo(width=1024, height=768, temperature=20)

$ ./send_image.py ESP_IP clear
# Clears the screen

$ ./send_image.y ESP_IP draw /tmp/spooder-man.png
# Draws on screen
```
Thanks to argparse, all arguments are visible with `--help`.  
Requires `requests` and `PIL` (or Pillow)

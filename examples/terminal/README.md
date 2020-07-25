Known Issues:

- With ESP-IDF 4.0, there is seems to be an issue with the UART 
  leading to missing / delayed data in some cases.
  Seems to be gone in 4.1. Enabling UART ISR in IRAM might help.

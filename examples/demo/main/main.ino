/**
 * This is the Arduino wrapper for the "Demo" example.
 * Please go to the main.c for the main example file.
 *
 * This example was developed for the ESP IoT Development Framework (IDF).
 * You can still use this code in the Arduino IDE, but it may not look
 * and feel like a classic Arduino sketch.
 * If you are looking for an example with Arduino look-and-feel,
 * please check the other examples.
 */

// Important: These are C functions, so they must be declared with C linkage!
extern "C"
{
    // Since Arduino does not allow selection
    // of numeric values in its menu, this
    // constant must be defined to set VCOM for v6 boards.
    int epd_driver_v6_vcom = 1560;

    void idf_setup();
    void idf_loop();
}

void setup() {
    if(psramInit()){
        Serial.println("\nThe PSRAM is correctly initialized");
    } else{
        Serial.println("\nPSRAM does not work");
    }

    idf_setup();
}

void loop() {
    idf_loop();
}

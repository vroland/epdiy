// Note: Run menuconfig to and set the right EPD display and board for your epaper.

// This example does not use a decoded buffer hence leaves more external RAM free
// and it uses a different JPEG decoder: https://github.com/bitbank2/JPEGDEC
// This decoder is not included and should be placed in the components directory
// Check this for a reference on how to do it : https://github.com/martinberlin/cale-idf/tree/master/main/www-jpg-render
// Adding this CMakeLists.txt file is required:
/*
set(srcs 
    "JPEGDEC.cpp"
    "jpeg.c"
)
idf_component_register(SRCS ${srcs}      
                    REQUIRES "jpegdec"
                    INCLUDE_DIRS "include")
*/
// Open settings to set WiFi and other configurations for both examples:
#include "settings.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
// WiFi related
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
// HTTP Client + time
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
// C
#include <stdio.h>
#include <string.h>
#include <math.h> // round + pow
extern "C" {
  #include "epd_driver.h"
  #include "epd_highlevel.h"
}
EpdiyHighlevelState hl;
// JPG decoder from @bitbank2
#include "JPEGDEC.h"

JPEGDEC jpeg;
// EXPERIMENTAL: If JPEG_CPY_FRAMEBUFFER is true the JPG is decoded directly in EPD framebuffer
// On true it looses rotation. Experimental, does not work alright yet. Hint:
// Check if an uint16_t buffer can be copied in a uint8_t buffer directly
#define JPEG_CPY_FRAMEBUFFER true

// Dither space allocation
uint8_t * dither_space;

// Internal array for gamma grayscale
uint8_t gamme_curve[256];

extern "C"
{
    void app_main();
}

// Load the EMBED_TXTFILES. Then doing (char*) server_cert_pem_start you get the SSL certificate
// Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#embedding-binary-data
extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");


// Buffers
uint8_t *fb;            // EPD 2bpp buffer
uint8_t *source_buf;    // JPG download buffer

uint32_t buffer_pos = 0;
uint32_t time_download = 0;
uint32_t time_decomp = 0;
uint32_t time_render = 0;
uint16_t ep_width = 0;
uint16_t ep_height = 0;

static const char *TAG = "Jpgdec";
uint16_t countDataEventCalls = 0;
uint32_t countDataBytes = 0;
uint32_t img_buf_pos = 0;
uint64_t startTime = 0;


#if VALIDATE_SSL_CERTIFICATE == true
  /* Time aware for ESP32: Important to check SSL certs validity */
  void time_sync_notification_cb(struct timeval *tv)
  {
      ESP_LOGI(TAG, "Notification of a time synchronization event");
  }

  static void initialize_sntp(void)
  {
      ESP_LOGI(TAG, "Initializing SNTP");
      sntp_setoperatingmode(SNTP_OPMODE_POLL);
      sntp_setservername(0, "pool.ntp.org");
      sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  #ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
      sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
  #endif
      sntp_init();
  }

  static void obtain_time(void)
  {
      initialize_sntp();

      // wait for time to be set
      time_t now = 0;
      struct tm timeinfo = { 0 };
      int retry = 0;
      const int retry_count = 10;
      while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
          ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
          vTaskDelay(2000 / portTICK_PERIOD_MS);
      }
      time(&now);
      localtime_r(&now, &timeinfo);
  }
#endif
//====================================================================================
// This sketch contains support functions to render the Jpeg images
//
// Created by Bitbank
// Refactored by @martinberlin for EPDiy as a Jpeg download and render example
//====================================================================================

/*
 * Used with jpeg.setPixelType(FOUR_BIT_DITHERED)
 */
uint16_t mcu_count = 0;
int JPEGDraw4Bits(JPEGDRAW *pDraw)
{
  uint32_t render_start = esp_timer_get_time();

  #if JPEG_CPY_FRAMEBUFFER
  // Highly experimental: Does not support rotation and gamma correction
  // Can be washed out compared to JPEG_CPY_FRAMEBUFFER false
  for (uint16_t yy = 0; yy < pDraw->iHeight; yy++) {
    // Copy directly horizontal MCU pixels in EPD fb
    memcpy(&fb[(pDraw->y+yy) * EPD_WIDTH / 2 + pDraw->x / 2], &pDraw->pPixels[(yy * pDraw->iWidth)>>2], pDraw->iWidth);
  }

  #else 
    // Rotation aware
    for (int16_t xx = 0; xx < pDraw->iWidth; xx+=4) {
      for (int16_t yy = 0; yy < pDraw->iHeight; yy++) {
        uint16_t col = pDraw->pPixels[ (xx + (yy * pDraw->iWidth)) >>2 ];
      
        uint8_t col1 = col & 0xf;
        uint8_t col2 = (col >> 4) & 0xf;
        uint8_t col3 = (col >> 8) & 0xf;
        uint8_t col4 = (col >> 12) & 0xf;
        epd_draw_pixel(pDraw->x + xx, pDraw->y + yy, gamme_curve[col1 *16], fb);
        epd_draw_pixel(pDraw->x + xx + 1, pDraw->y + yy, gamme_curve[col2 *16], fb);
        epd_draw_pixel(pDraw->x + xx + 2, pDraw->y + yy, gamme_curve[col3 *16], fb);
        epd_draw_pixel(pDraw->x + xx + 3, pDraw->y + yy, gamme_curve[col4 *16], fb);

        /* if (yy==0 && mcu_count==0) {
          printf("1.%d %d %d %d ",col1,col2,col3,col4);
        } */
      }
    }
  #endif

  mcu_count++;
  time_render += (esp_timer_get_time() - render_start) / 1000;
  return 1;
}

void deepsleep(){
    esp_deep_sleep(1000000LL * 60 * DEEPSLEEP_MINUTES_AFTER_RENDER);
}

//====================================================================================
//   This function opens source_buf Jpeg image file and primes the decoder
//====================================================================================
int decodeJpeg(uint8_t *source_buf, int xpos, int ypos) {
  uint32_t decode_start = esp_timer_get_time();

  if (jpeg.openRAM(source_buf, img_buf_pos, JPEGDraw4Bits)) {

    jpeg.setPixelType(FOUR_BIT_DITHERED);
    
    if (jpeg.decodeDither(dither_space, 0))
      {
        time_decomp = (esp_timer_get_time() - decode_start)/1000 - time_render;
        ESP_LOGI("decode", "%d ms - %dx%d image MCUs:%d", time_decomp, jpeg.getWidth(), jpeg.getHeight(), mcu_count);
      } else {
        ESP_LOGE("jpeg.decode", "Failed with error: %d", jpeg.getLastError());
      }

  } else {
    ESP_LOGE("jpeg.openRAM", "Failed with error: %d", jpeg.getLastError());
  }
  jpeg.close();
  
  return 1;
}

// Handles Htpp events and is in charge of buffering source_buf (jpg compressed image)
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        #if DEBUG_VERBOSE
          ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        #endif
        break;
    case HTTP_EVENT_ON_DATA:
        ++countDataEventCalls;
        #if DEBUG_VERBOSE
          if (countDataEventCalls%10==0) {
            ESP_LOGI(TAG, "%d len:%d\n", countDataEventCalls, evt->data_len);
          }
        #endif
        

        if (countDataEventCalls == 1) startTime = esp_timer_get_time();
        // Append received data into source_buf
        memcpy(&source_buf[img_buf_pos], evt->data, evt->data_len);
        img_buf_pos += evt->data_len;
        break;

    case HTTP_EVENT_ON_FINISH:
        // Do not draw if it's a redirect (302)
        if (esp_http_client_get_status_code(evt->client) == 200) {
          printf("%d bytes read from %s\n", img_buf_pos, IMG_URL);
          time_download = (esp_timer_get_time()-startTime)/1000;

          decodeJpeg(source_buf, 0, 0);
          
          ESP_LOGI("www-dw", "%d ms - download", time_download);
          ESP_LOGI("render", "%d ms - copying pix (JPEG_CPY_FRAMEBUFFER:%d)", time_render, JPEG_CPY_FRAMEBUFFER);
          // Refresh display
         epd_hl_update_screen(&hl, MODE_GC16, 25);

          ESP_LOGI("total", "%d ms - total time spent\n", time_download+time_decomp+time_render);
        } else {
          printf("HTTP on finish got status code: %d\n", esp_http_client_get_status_code(evt->client));
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
        break;
    }
    return ESP_OK;
}

// Handles http request
static void http_post(void)
{    
    /**
     * NOTE: All the configuration parameters for http_client must be specified
     * either in URL or as host and path parameters.
     */
    esp_http_client_config_t config = {
        .url = IMG_URL,
        #if VALIDATE_SSL_CERTIFICATE == true
        .cert_pem = (char *)server_cert_pem_start,
        #endif
        .disable_auto_redirect = false,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE
        };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    #if DEBUG_VERBOSE
      printf("Free heap before HTTP download: %d\n", xPortGetFreeHeapSize());
      if (esp_http_client_get_transport_type(client) == HTTP_TRANSPORT_OVER_SSL && config.cert_pem) {
        printf("SSL CERT:\n%s\n\n", (char *)server_cert_pem_start);
      }
    #endif
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "\nIMAGE URL: %s\n\nHTTP GET Status = %d, content_length = %d\n",
                 IMG_URL,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "\nHTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    
    #if MILLIS_DELAY_BEFORE_SLEEP>0
      vTaskDelay(MILLIS_DELAY_BEFORE_SLEEP / portTICK_PERIOD_MS);
    #endif
    printf("Go to sleep %d minutes\n", DEEPSLEEP_MINUTES_AFTER_RENDER);
    epd_poweroff();
    deepsleep();
}

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < 5)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Connect to the AP failed %d times. Going to deepsleep %d minutes", 5, DEEPSLEEP_MINUTES_AFTER_RENDER);
            deepsleep();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initializes WiFi the ESP-IDF way
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    // C++ wifi config
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), ESP_WIFI_SSID);
    sprintf(reinterpret_cast<char *>(wifi_config.sta.password), ESP_WIFI_PASSWORD);
    wifi_config.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to ap SSID:%s", ESP_WIFI_SSID);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", ESP_WIFI_SSID);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void app_main() {
  epd_init(EPD_OPTIONS_DEFAULT);
  hl = epd_hl_init(WAVEFORM);
  fb = epd_hl_get_framebuffer(&hl);

  printf("JPGDEC version @bitbank2\n");
  dither_space = (uint8_t *)heap_caps_malloc(EPD_WIDTH *16, MALLOC_CAP_SPIRAM);
  if (dither_space == NULL) {
      ESP_LOGE("main", "Initial alloc ditherSpace failed!");
  }

  // Should be big enough to allocate the JPEG file size, width * height should suffice
  source_buf = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT, MALLOC_CAP_SPIRAM);
  if (source_buf == NULL) {
      ESP_LOGE("main", "Initial alloc source_buf failed!");
  }
  printf("Free heap after buffers allocation: %d\n", xPortGetFreeHeapSize());

  double gammaCorrection = 1.0 / gamma_value;
  for (int gray_value =0; gray_value<256;gray_value++)
    gamme_curve[gray_value]= round (255*pow(gray_value/255.0, gammaCorrection));
  
  #if JPEG_CPY_FRAMEBUFFER == false
    epd_set_rotation(DISPLAY_ROTATION);
  #endif

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // WiFi log level set only to Error otherwise outputs too much
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  epd_poweron();
  epd_fullclear(&hl, 25);

  // Initialization: WiFi + clean screen while downloading image
  printf("Free heap before wifi_init_sta: %d\n", xPortGetFreeHeapSize());
  wifi_init_sta();
  #if VALIDATE_SSL_CERTIFICATE == true
    obtain_time();
  #endif
  
  http_post();
}

// Open settings.h to set WiFi and other configurations
// jpeg-render-color is deprecated. Please use jpegdec-render.cpp example and set the right DISPLAY_COLOR_TYPE (NONE for non Color-filter)
#include "settings.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_types.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
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

// JPG decoder is on ESP32 rom for this version
#include "rom/tjpgd.h"


#include <stdio.h>
#include <string.h>
//#include <math.h> // round + pow

#include "epdiy.h"
EpdiyHighlevelState hl;

// Load the EMBED_TXTFILES. Then doing (char*) server_cert_pem_start you get the SSL certificate
// Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#embedding-binary-data
extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");

// JPEG decoder
JDEC jd;
JRESULT rc;

// Buffers
uint8_t *fb;            // EPD 2bpp buffer
uint8_t *source_buf = NULL;    // JPG download buffer
uint8_t *decoded_image; // RAW decoded image
static uint8_t tjpgd_work[4096]; // tjpgd 3096 is the minimum size

uint32_t buffer_pos = 0;
uint32_t time_download = 0;
uint32_t time_decomp = 0;
uint32_t time_render = 0;
static const char * jd_errors[] = {
    "Succeeded",
    "Interrupted by output function",
    "Device error or wrong termination of input stream",
    "Insufficient memory pool for the image",
    "Insufficient stream input buffer",
    "Parameter error",
    "Data format error",
    "Right format but not supported",
    "Not supported JPEG standard"
};

//uint8_t gamme_curve[256];

static const char *TAG = "EPDiy";
uint16_t countDataEventCalls = 0;
uint32_t countDataBytes = 0;
uint32_t img_buf_pos = 0;
uint32_t dataLenTotal = 0;
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
          ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", (int)retry, (int)retry_count);
          vTaskDelay(2000 / portTICK_PERIOD_MS);
      }
      time(&now);
      localtime_r(&now, &timeinfo);
  }
#endif
//====================================================================================
// This sketch contains support functions to render the Jpeg images
//
// Created by Bodmer 15th Jan 2017
// Refactored by @martinberlin for EPDiy as a Jpeg download and render example
//====================================================================================

// Return the minimum of two values a and b
#define minimum(a,b)     (((a) < (b)) ? (a) : (b))

uint8_t find_closest_palette_color(uint8_t oldpixel)
{
  return oldpixel & 0xF0;
}

void deepsleep(){
    esp_deep_sleep(1000000LL * 60 * DEEPSLEEP_MINUTES_AFTER_RENDER);
}

static unsigned int feed_buffer(JDEC *jd,
               unsigned char* buff, // Pointer to the read buffer (NULL:skip)
               unsigned int nd
) {
    uint32_t count = 0;

    while (count < nd) {
      if (buff != NULL) {
            *buff++ = source_buf[buffer_pos];
        }
        count ++;
        buffer_pos++;
    }

  return count;
}

/* User defined call-back function to output decoded RGB bitmap in decoded_image buffer */
static unsigned int tjd_output(
    JDEC *jd,     /* Decompressor object of current session */
    void *bitmap, /* Bitmap data to be output */
    JRECT *rect   /* Rectangular region to output */
) {
  uint32_t w = rect->right - rect->left + 1;
  uint32_t h = rect->bottom - rect->top + 1;
  uint32_t image_width = jd->width;
  uint8_t *bitmap_ptr = (uint8_t*)bitmap;

  for (uint32_t i = 0; i < w * h; i++) {

    uint8_t r = *(bitmap_ptr++);
    uint8_t g = *(bitmap_ptr++);
    uint8_t b = *(bitmap_ptr++);

    int xx = rect->left + i % w;
    if (xx < 0 || xx >= image_width) {
      continue;
    }
    int yy = rect->top + i / w;
    if (yy < 0 || yy >= jd->height) {
      continue;
    }

    /* Optimization note: If we manage to apply here the epd_draw_pixel directly
       then it will be no need to keep a huge raw buffer (But will loose dither) */
    epd_draw_cpixel(xx, yy, r, g, b, hl.front_fb);
  }
  
  vTaskDelay(0);
  return 1;
}

//====================================================================================
//   This function opens source_buf Jpeg image file and primes the decoder
//====================================================================================
int drawBufJpeg(uint8_t *source_buf, int xpos, int ypos) {
  rc = jd_prepare(&jd, feed_buffer, tjpgd_work, sizeof(tjpgd_work), &source_buf);
  if (rc != JDR_OK) {
    ESP_LOGE(TAG, "JPG jd_prepare error: %s", jd_errors[rc]);
    return ESP_FAIL;
  }

  uint32_t decode_start = esp_timer_get_time();

  // Last parameter scales        v 1 will reduce the image
  rc = jd_decomp(&jd, tjd_output, 0);
  if (rc != JDR_OK) {
    ESP_LOGE(TAG, "JPG jd_decomp error: %s", jd_errors[rc]);
    return ESP_FAIL;
  }

  time_decomp = (esp_timer_get_time() - decode_start)/1000;

  ESP_LOGI("JPG", "width: %d height: %d\n", jd.width, jd.height);
  ESP_LOGI("decode", "%d ms . image decompression", (int)time_decomp);

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
        // Allocate JPG buffer
        if (strncmp(evt->header_key, "Content-Length", 14) == 0) {
            // Should be big enough to hold the JPEG file size
            size_t content_len = atol(evt->header_value);
            ESP_LOGI("epdiy", "Allocating content buffer of length %X", content_len);

            source_buf = (uint8_t*)heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);

            if (source_buf == NULL) {
                ESP_LOGE("main", "Initial alloc source_buf failed!");
            }

            printf("Free heap after buffers allocation: %d\n", xPortGetFreeHeapSize());
        }
        break;
    case HTTP_EVENT_ON_DATA:
        ++countDataEventCalls;
        #if DEBUG_VERBOSE
          if (countDataEventCalls%10==0) {
            ESP_LOGI(TAG, "%d len:%d\n", (int)countDataEventCalls, (int)evt->data_len);
          }
        #endif
        dataLenTotal += evt->data_len;

        if (countDataEventCalls == 1) startTime = esp_timer_get_time();
        // Append received data into source_buf
        memcpy(&source_buf[img_buf_pos], evt->data, evt->data_len);
        img_buf_pos += evt->data_len;

        // Optional hexa dump
        //ESP_LOG_BUFFER_HEX(TAG, output_buffer, evt->data_len);
        break;

    case HTTP_EVENT_ON_FINISH:
        // Do not draw if it's a redirect (302)
        if (esp_http_client_get_status_code(evt->client) == 200) {
          //printf("%lld bytes read from %s\n\n", img_buf_pos, IMG_URL);
          drawBufJpeg(source_buf, 0, 0);
          time_download = (esp_timer_get_time()-startTime)/1000;
          ESP_LOGI("www-dw", "%ld ms - download", time_download);
          // Refresh display
          epd_hl_update_screen(&hl, MODE_GC16, 25);

          ESP_LOGI("total", "%ld ms - total time spent\n", time_download+time_decomp+time_render);

          #if MILLIS_DELAY_BEFORE_SLEEP>0
            vTaskDelay(MILLIS_DELAY_BEFORE_SLEEP / portTICK_PERIOD_MS);
          #endif
          printf("Poweroff EPD and go to sleep %d minutes\n", DEEPSLEEP_MINUTES_AFTER_RENDER);
          epd_poweroff();
          vTaskDelay(5); 
          deepsleep();
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
        break;

    default:
        ESP_LOGI(TAG, "HTTP_EVENT_ fallback caught event %d", evt->event_id);
    }
    return ESP_OK;
}

// Handles http request
static void http_post(void)
{
    /**
     * NOTE: All the configuration parameters for http_client must be specified
     * either in URL or as host and path parameters.
     * FIX: Uncommenting cert_pem restarts even if providing the right certificate
     */
    esp_http_client_config_t config = {
        .url = IMG_URL,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE,
        .disable_auto_redirect = false,
        #if VALIDATE_SSL_CERTIFICATE == true
        .cert_pem = (char *)server_cert_pem_start
        #endif
        };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    #if DEBUG_VERBOSE
      printf("Free heap before HTTP download: %d\n", (int)xPortGetFreeHeapSize());
      if (esp_http_client_get_transport_type(client) == HTTP_TRANSPORT_OVER_SSL && config.cert_pem) {
        printf("SSL CERT:\n%s\n\n", (char *)server_cert_pem_start);
      }
    #endif

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        // esp_http_client_get_content_length returns a uint64_t in esp-idf v5, so it needs a %lld format specifier

        printf("\nIMAGE URL: %s\n\nHTTP GET Status = %d, content_length = %lld\n",
                 IMG_URL,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "\nHTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    
    esp_http_client_cleanup(client);
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

    wifi_config_t wifi_config = {
    .sta = {
        .ssid = ESP_WIFI_SSID,
        .password = ESP_WIFI_PASSWORD,
        /* Setting a password implies station will connect to all security modes including WEP/WPA.
            * However these modes are deprecated and not advisable to be used. Incase your Access point
            * doesn't support WPA2, these mode can be enabled by commenting below line */
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        .pmf_cfg = {
            .capable = true,
            .required = false
        },
    },
    };

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

int getPsramSize(){
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    return info.total_free_bytes + info.total_allocated_bytes;
}
int getFreePsram(){
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    return info.total_free_bytes;
}

void app_main() {
  // NVS is needed for WiFi
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // WiFi log level set only to Error otherwise outputs too much
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  epd_init(&epd_board_v7_raw, &EC060KH5, EPD_LUT_64K);
  // Set VCOM for boards that allow to set this in software (in mV).
  epd_set_vcom(2000);
  
  wifi_init_sta();

  
  printf("before epd_hl_init() PSRAM: %d\n", getFreePsram());
  hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
  fb = epd_hl_get_framebuffer(&hl);
  epd_set_gamma_curve(gamma_value);
  epd_set_rotation(DISPLAY_ROTATION);
  printf("after epd_hl_init() Free PSRAM: %d\n", getFreePsram());

  #if VALIDATE_SSL_CERTIFICATE == true
    obtain_time();
  #endif

  epd_poweron();
  epd_fullclear(&hl, 25);
  
  http_post();
}

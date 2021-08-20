#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_sleep.h"
#include "string.h"

// WiFi related
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
// HTTP Client
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_tls.h"
#include "esp_http_client.h"

// JPG decoder
#if ESP_IDF_VERSION_MAJOR >= 4 // IDF 4+
  #include "esp32/rom/tjpgd.h"
#else // ESP32 Before IDF 4.0
  #include "rom/tjpgd.h"
#endif

#include "esp_task_wdt.h"
#include <stdio.h>
#include <string.h>
#include <math.h> // round + pow

#include "epd_driver.h"
#include "epd_highlevel.h"
EpdiyHighlevelState hl;

// www URL of the JPG image
// Note: Only HTTP protocol supported (SSL secure URLs not supported yet  )
#define IMG_URL "http://img.cale.es/jpg/fasani/5e636b0f39aac"

// Adds dithering to render image (minimally changes)
#define JPG_DITHERING true
// WiFi configuration
#define ESP_WIFI_SSID     "WLAN-724300"
#define ESP_WIFI_PASSWORD "50238634630558382093"
// Minutes that goes to deepsleep after rendering
// If you build a gallery URL that returns a new image on each request (like cale.es)
// this parameter can be interesting to make an automatic photo-slider
#define DEEPSLEEP_MINUTES_AFTER_RENDER 4
#define DEBUG_VERBOSE false

// JPEG decoder buffers
JDEC jd; 
JRESULT rc;
uint8_t *img_buf;
uint8_t *source_buf;
uint8_t *decoded_image;
static uint8_t tjpgd_work[4096];
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

const uint16_t ep_width=EPD_WIDTH;
const uint16_t ep_height=EPD_HEIGHT;

uint16_t this_pic=0;

double gamma_value = 1.5;
uint8_t gamme_curve[256];

static const char *TAG = "EPDiy";
// As default is 512 without setting buffer_size property in esp_http_client_config_t
#define HTTP_RECEIVE_BUFFER_SIZE 1536

char espIpAddress[16];
uint16_t countDataEventCalls = 0;
uint32_t countDataBytes = 0;
uint32_t img_buf_pos = 0;
uint32_t dataLenTotal = 0;
uint64_t startTime = 0;

/*====================================================================================
  This sketch contains support functions to render the Jpeg images.

  Created by Bodmer 15th Jan 2017
  ==================================================================================*/

// Return the minimum of two values a and b
#define minimum(a,b)     (((a) < (b)) ? (a) : (b))

typedef struct {
    int scale;
    uint8_t* decoded_image;
    uint8_t* source_img;
    uint32_t source_width;
    uint32_t source_height;
} ImageDecodeContext_t;


uint8_t find_closest_palette_color(uint8_t oldpixel)
{
  return (round((oldpixel / 16)*16));
}
//====================================================================================
//   Decode and paint onto the TFT screen
//====================================================================================
void jpegRender(int xpos, int ypos, ImageDecodeContext_t* context) {
 #if JPG_DITHERING 
 unsigned long pixel=0;
 for (uint16_t by=0; by<ep_height;by++)
  {
    for (uint16_t bx=0; bx<ep_width;bx++)
    {
        int oldpixel = decoded_image[pixel];
        int newpixel = find_closest_palette_color(oldpixel);
        int quant_error = oldpixel - newpixel;
        decoded_image[pixel]=newpixel;
        if (bx<(ep_width-1))
          decoded_image[pixel+1] = minimum(255,decoded_image[pixel+1] + quant_error * 7 / 16);

        if (by<(ep_height-1))
        {
          if (bx>0)
            decoded_image[pixel+ep_width-1] =  minimum(255,decoded_image[pixel+ep_width-1] + quant_error * 3 / 16);

          decoded_image[pixel+ep_width] =  minimum(255,decoded_image[pixel+ep_width] + quant_error * 5 / 16);
          if (bx<(ep_width-1))
            decoded_image[pixel+ep_width+1] = minimum(255,decoded_image[pixel+ep_width+1] + quant_error * 1 / 16);
        }
        pixel++;
    }
  }
  #endif

  // Write to display
  uint64_t drawTime = esp_timer_get_time();
  memset(img_buf, 255, EPD_WIDTH/2 * EPD_HEIGHT);
  uint32_t padding_x = (EPD_WIDTH - context->source_width) / 2;
  uint32_t padding_y = (EPD_HEIGHT - context->source_height) / 2;

  for (uint32_t by=0; by<context->source_height;by++) {
    for (uint32_t bx=0; bx<context->source_width;bx++) {
        epd_draw_pixel(bx + padding_x, by + padding_y, decoded_image[by * context->source_width + bx], img_buf);
    }
  }
  // calculate how long it took to draw the image
  time_render = (esp_timer_get_time() - drawTime)/1000;
  ESP_LOGI("render", "%d ms - jpeg draw", time_render);
}

void deepsleep(){
    esp_deep_sleep(1000000LL * 60 * DEEPSLEEP_MINUTES_AFTER_RENDER);
}

static uint32_t feed_buffer(JDEC *jd,      
               uint8_t *buff, // Pointer to the read buffer (NULL:skip) 
               uint32_t nd 
) {
    vTaskDelay(1);
    esp_task_wdt_reset();
    uint32_t count = 0;

    while (count < nd) {

      if (buff != NULL) {
            uint8_t b = source_buf[buffer_pos]; 
            //printf("%x ", b);
            *buff++ = b;
        }
        count ++;
        buffer_pos++;
    }
  return count;
}

/* User defined call-back function to output RGB bitmap */
static uint32_t
tjd_output(JDEC *jd,     /* Decompressor object of current session */
           void *bitmap, /* Bitmap data to be output */
           JRECT *rect   /* Rectangular region to output */
) {
  esp_task_wdt_reset();

  uint32_t w = rect->right - rect->left + 1;
  uint32_t h = rect->bottom - rect->top + 1;
  uint32_t image_width = jd->width; // >> context->scale

  uint8_t *bitmap_ptr = (uint8_t*)bitmap;
  
  //printf("w %d h %d iw %d ", w,h,image_width); // image_width should not be 0 as happened with >>context->scale

  for (uint32_t i = 0; i < w * h; i++) {

    uint8_t r = *(bitmap_ptr++);
    uint8_t g = *(bitmap_ptr++);
    uint8_t b = *(bitmap_ptr++);

    // Calculate weighted grayscale
    uint32_t val = ((r * 30 + g * 59 + b * 11) / 100); //old formula
    //uint32_t val = (r*38 + g*75 + b*15) >> 7; // @vroland recommended formula

    int xx = rect->left + i % w;
    if (xx < 0 || xx >= image_width) {
      continue;
    }
    int yy = rect->top + i / w;
    if (yy < 0 || yy >= image_width) {
      continue;
    }
    
    decoded_image[yy * image_width + xx] = gamme_curve[val];
    // Check if image pixels are in fact being drawed with right grayscale
    //printf("di[%d]=%d ", yy * image_width + xx,  gamme_curve[val]);
  }

  return 1;
}

//====================================================================================
//   This function opens source_buf Jpeg image file and primes the decoder
//====================================================================================
int drawBufJpeg(uint8_t *source_buf, int xpos, int ypos) {
  ImageDecodeContext_t dc;
  dc.scale = 1;

  rc = jd_prepare(&jd, feed_buffer, tjpgd_work, sizeof(tjpgd_work), &source_buf);
  if (rc != JDR_OK) {    
    ESP_LOGE(TAG, "JPG jd_prepare error: %s", jd_errors[rc]);
    return ESP_FAIL;
  }

  for (dc.scale = 0; dc.scale < 3; dc.scale++) {
    if ((jd.width >> dc.scale) <= EPD_WIDTH && (jd.height >> dc.scale) <= EPD_HEIGHT)
      break;
  }

  uint32_t width = jd.width >> dc.scale;
  uint32_t height = jd.height >> dc.scale;
  dc.source_width = width;
  dc.source_height = height;
  #if DEBUG_VERBOSE
    printf("orig width: %d orig height: %d\n", jd.width, jd.height);
    printf("scaled width: %d scaled height: %d\n", width, height);
  #endif
  uint32_t decode_start = esp_timer_get_time();
  rc = jd_decomp(&jd, tjd_output, dc.scale);
  if (rc != JDR_OK) {
    ESP_LOGE(TAG, "JPG jd_decomp error: %s", jd_errors[rc]);
    return ESP_FAIL;
  }
  uint32_t decode_end = esp_timer_get_time();
  time_decomp = (decode_end - decode_start)/1000;
  ESP_LOGI("decode", "%d ms . image decompression", time_decomp);

  // Render the image onto the screen at given coordinates
  jpegRender(xpos, ypos, &dc);

  return 0;
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    uint8_t output_buffer[HTTP_RECEIVE_BUFFER_SIZE]; // Buffer to store HTTP response

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
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ++countDataEventCalls;
        if (countDataEventCalls%10==0) {
        ESP_LOGI(TAG, "%d len:%d\n", countDataEventCalls, evt->data_len); }
        dataLenTotal += evt->data_len;
        
        // Copy the response into the buffer
        memcpy(output_buffer, evt->data, evt->data_len);

        if (countDataEventCalls == 1)
        {
            startTime = esp_timer_get_time();
        }
        
        // LOOP all the received Buffer but start on ImageOffset if first call
        for (uint32_t byteIndex = 0; byteIndex < evt->data_len; ++byteIndex)
        {
            source_buf[img_buf_pos] = output_buffer[byteIndex];
            img_buf_pos++;
        }

        // Hexa dump:
        //ESP_LOG_BUFFER_HEX(TAG, output_buffer, evt->data_len);
        break;

    case HTTP_EVENT_ON_FINISH:
        printf("%d bytes read from %s\n\n", img_buf_pos, IMG_URL);
        
        drawBufJpeg(source_buf, 0, 0);
        time_download = (esp_timer_get_time()-startTime)/1000;
        ESP_LOGI("www-dw", "%d ms - download", time_download);
        // Refresh display
        epd_hl_update_screen(&hl, MODE_GC16, 25);

        ESP_LOGI("total", "%d ms - total time spent\n", time_download+time_decomp+time_render);

        printf("Refresh and go to sleep %d minutes\n", DEEPSLEEP_MINUTES_AFTER_RENDER);
        vTaskDelay(10);
        deepsleep();
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
        break;
    }
    return ESP_OK;
}

static void http_post(void)
{
    /**
     * NOTE: All the configuration parameters for http_client must be spefied
     * either in URL or as host and path parameters.
     */
    esp_http_client_config_t config = {
        .url = IMG_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE
        };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
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
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sprintf(espIpAddress,  IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s\n", espIpAddress);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

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
        ESP_LOGI(TAG, "connected to ap SSID:%s", ESP_WIFI_SSID);
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
  hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
  img_buf = epd_hl_get_framebuffer(&hl);

  // For 4.7" resolution: 960*540*3   (Aprox. 1.6 MB)
  decoded_image = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT * 3, MALLOC_CAP_SPIRAM);
  if (decoded_image == NULL) {
      ESP_LOGE("main", "initial alloc back_buf failed!");
  }
  memset(decoded_image, 255, EPD_WIDTH * EPD_HEIGHT * 3);

  // Should be big enough to handle the JPEG file size
  source_buf = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT, MALLOC_CAP_SPIRAM);
  if (source_buf == NULL) {
      ESP_LOGE("main", "initial alloc source_buf failed!");
  }

  printf("heap allocated\n");

  double gammaCorrection = 1.0 / gamma_value;
  for (int gray_value =0; gray_value<256;gray_value++)
    gamme_curve[gray_value]= round (255*pow(gray_value/255.0, gammaCorrection));

  //Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // WiFi log level set only to Error otherwise outputs too much
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  wifi_init_sta();
  epd_poweron();

  epd_clear();

  http_post();
}

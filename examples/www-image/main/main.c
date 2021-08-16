/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

// - - - - HTTP Client
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_sleep.h"
#include "string.h"

// - - - - EPDiy includes
#include "epd_driver.h"
#include "epd_highlevel.h"
EpdiyHighlevelState hl;
uint8_t* fb;

// www URL of the bitmap image (Should be BMP format 1,4,8 or 24 bits-depth)
// Note: Only HTTP protocol supported (SSL secure URLs not supported yet   )
#define IMG_URL "http://img.cale.es/bmp/fasani/5e636b0f39aac"
// WiFi configuration
#define ESP_WIFI_SSID     "WLAN-724300"
#define ESP_WIFI_PASSWORD "50238634630558382093"
// Minutes that goes to deepsleep after rendering
#define DEEPSLEEP_MINUTES_AFTER_RENDER 10

// BMP debug Mode: Turn false for production since it will make things slower and dump Serial debug
bool bmpDebug = false;

// IP is sent per post for logging purpouses.
char espIpAddress[16];
char bearerToken[74] = "";
// As default is 512 without setting buffer_size property in esp_http_client_config_t
#define HTTP_RECEIVE_BUFFER_SIZE 1024

static const char *TAG = "CALE";

uint16_t countDataEventCalls = 0;
uint32_t countDataBytes = 0;

struct BmpHeader
{
    uint32_t fileSize;
    uint32_t imageOffset;
    uint32_t headerSize;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t depth;
    uint32_t format;
} bmp;

uint16_t read16(uint8_t output_buffer[512], uint8_t startPointer)
{
    // BMP data is stored little-endian
    uint16_t result;
    ((uint8_t *)&result)[0] = output_buffer[startPointer];     // LSB
    ((uint8_t *)&result)[1] = output_buffer[startPointer + 1]; // MSB
    return result;
}

uint32_t read32(uint8_t output_buffer[512], uint8_t startPointer)
{
    //Debug - Leave disabled to avoid Serial output
    //printf("read32: %x %x %x %x\n", output_buffer[startPointer],output_buffer[startPointer+1],output_buffer[startPointer+2],output_buffer[startPointer+3]);
    uint32_t result;
    ((uint8_t *)&result)[0] = output_buffer[startPointer]; // LSB
    ((uint8_t *)&result)[1] = output_buffer[startPointer + 1];
    ((uint8_t *)&result)[2] = output_buffer[startPointer + 2];
    ((uint8_t *)&result)[3] = output_buffer[startPointer + 3]; // MSB
    return result;
}

uint16_t in_red = 0;   // for depth 24
uint16_t in_green = 0; // for depth 24
uint16_t in_blue = 0;  // for depth 24

uint32_t rowSize;
uint32_t rowByteCounter;
uint16_t w;
uint16_t h;
uint8_t bitmask = 0xFF;
uint8_t bitshift, whitish, red, green, blue;
uint16_t drawX = 0;
uint16_t drawY = 0;
uint8_t index24 = 0; // Index for 24 bit
uint16_t bPointer = 34; // Byte pointer - Attention drawPixel has uint16_t
uint16_t imageBytesRead = 0;
uint32_t dataLenTotal = 0;
uint32_t in_bytes = 0;
uint8_t in_byte = 0; // for depth <= 8
uint8_t in_bits = 0; // for depth <= 8
bool isReadingImage = false;
bool isSupportedBitmap = true;
bool isPaddingAware = false;
uint16_t forCount = 0;

uint8_t mono_palette_buffer[32];        // palette buffer for depth <= 8 b/w

uint16_t totalDrawPixels = 0;
int color = 0xff;
uint64_t startTime = 0;

void deepsleep(){
    esp_deep_sleep(1000000LL * 60 * DEEPSLEEP_MINUTES_AFTER_RENDER);
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
        // Unless bmp.imageOffset initial skip we start reading stream always on byte pointer 0:
        bPointer = 0;
        // Copy the response into the buffer
        memcpy(output_buffer, evt->data, evt->data_len);

        if (countDataEventCalls == 1)
        {
            startTime = esp_timer_get_time();
            // Read BMP header -In total 34 bytes header
            bmp.fileSize = read32(output_buffer, 2);
            bmp.imageOffset = read32(output_buffer, 10);
            bmp.headerSize = read32(output_buffer, 14);
            bmp.width = read32(output_buffer, 18);
            bmp.height = read32(output_buffer, 22);
            bmp.planes = read16(output_buffer, 26);
            bmp.depth = read16(output_buffer, 28);
            bmp.format = read32(output_buffer, 30);

            drawY = bmp.height;
            ESP_LOGI(TAG, "BMP HEADERS\nfilesize:%d\noffset:%d\nW:%d\nH:%d\nplanes:%d\ndepth:%d\nformat:%d\n",
                     bmp.fileSize, bmp.imageOffset, bmp.width, bmp.height, bmp.planes, bmp.depth, bmp.format);

            if (bmp.depth == 1)
            {
                isPaddingAware = true;
                ESP_LOGI(TAG, "BMP isPaddingAware:  1 bit depth are 4 bit padded. Wikipedia gave me a lesson.");
            }
            if (((bmp.planes == 1) && ((bmp.format == 0) || (bmp.format == 3))) == false)
            { // uncompressed is handled
                isSupportedBitmap = false;
                ESP_LOGE(TAG, "BMP NOT SUPPORTED: Compressed formats not handled.\nBMP NOT SUPPORTED: Only planes==1, format 0 or 3\n");
            }
            if (bmp.depth == 4 || bmp.depth == 8 || bmp.depth == 16 || bmp.depth > 24)
            {
                isSupportedBitmap = false;
                ESP_LOGE(TAG, "BMP DEPTH %d: Only 1 and 24 bits depth are supported.\n", bmp.depth);
            }

            rowSize = (bmp.width * bmp.depth / 8 + 3) & ~3;
            if (bmp.depth < 8) {
                rowSize = ((bmp.width * bmp.depth + 8 - bmp.depth) / 8 + 3) & ~3;
                bitmask >>= bmp.depth;
                // Color-palette location:
                bPointer = bmp.imageOffset - (4 << bmp.depth);
                if (bmpDebug)
                    printf("Palette location: %d\n\n", bPointer);

                for (uint16_t pn = 0; pn < (1 << bmp.depth); pn++)
                {
                    blue = output_buffer[bPointer++];
                    green = output_buffer[bPointer++];
                    red = output_buffer[bPointer++];
                    bPointer++;

                    whitish = ((red > 0xF0) && (green > 0xF0) && (blue > 0xF0));

                    if (0 == pn % 8) {
                       mono_palette_buffer[pn / 8] = 0;
                     }
                    
                    mono_palette_buffer[pn / 8] |= whitish << pn % 8;
                }
            }

            if (bmpDebug)
                printf("ROW Size %d\n", rowSize);
            w = bmp.width;
            h = bmp.height;
            if ((w - 1) >= EPD_WIDTH)
                w = EPD_WIDTH;
            if ((h - 1) >= EPD_HEIGHT)
                h = EPD_HEIGHT;

            bitshift = 8 - bmp.depth;

            imageBytesRead += evt->data_len;
        }
        if (!isSupportedBitmap)
            return ESP_FAIL;

        if (bmpDebug)
        {
            printf("\n--> bPointer %d\n_inX: %d _inY: %d DATALEN TOTAL:%d bytesRead so far:%d\n",
                   bPointer, drawX, drawY, dataLenTotal, imageBytesRead);
            printf("Is reading image: %d\n", isReadingImage);
        }

        // Didn't arrived to imageOffset YET, it will in next calls of HTTP_EVENT_ON_DATA:
        if (dataLenTotal < bmp.imageOffset)
        {
            imageBytesRead = dataLenTotal;
            if (bmpDebug)
                printf("IF read<offset UPDATE bytesRead:%d\n", imageBytesRead);
            return ESP_OK;
        }
        else
        {
            // Only move pointer once to set right offset
            if (countDataEventCalls == 1 && bmp.imageOffset < evt->data_len)
            {
                bPointer = bmp.imageOffset;
                isReadingImage = true;
                printf("Offset comes in first DATA callback. bPointer: %d == bmp.imageOffset\n", bPointer);
            }
            if (!isReadingImage)
            {
                bPointer = bmp.imageOffset - imageBytesRead;
                imageBytesRead += bPointer;
                isReadingImage = true;
                printf("Start reading image. bPointer: %d\n", bPointer);
            }
        }
        forCount = 0;

        // LOOP all the received Buffer but start on ImageOffset if first call
        for (uint32_t byteIndex = bPointer; byteIndex < evt->data_len; ++byteIndex)
        {
            in_byte = output_buffer[byteIndex];
            
            // Dump only the first calls
            if (countDataEventCalls < 2 && bmpDebug)
            {
                printf("L%d: BrsF:%d %x\n", byteIndex, imageBytesRead, in_byte);
            }
            in_bits = 8;

            switch (bmp.depth)
            {
            case 1:
            {
                while (in_bits != 0)
                {

                    uint16_t pn = (in_byte >> bitshift) & bitmask;       
                    uint8_t white = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                    in_byte <<= bmp.depth;
                    in_bits -= bmp.depth;
                    if (white) {
                        color = 0xFF;
                    } else {
                        color = 0x00;
                    }

                    // bmp.width reached? Then go one line up (Is readed from bottom to top)
                    if (isPaddingAware)
                    { // 1 bit images are 4-bit padded (Filled usually with 0's)
                        if (drawX + 1 > rowSize * 8)
                        {
                            drawX = 0;
                            rowByteCounter = 0;
                            --drawY;
                        }
                    }
                    else
                    {
                        if (drawX + 1 > bmp.width)
                        {
                            drawX = 0;
                            rowByteCounter = 0;
                            --drawY;
                        }
                    }
                    // The ultimate mission: Send the X / Y pixel to the GFX Buffer
                    epd_draw_pixel(drawX, drawY, color, fb);

                    totalDrawPixels++;
                    ++drawX;
                }
            }
            break;

            case 24:
                // index24  3 byte B,G,R counter starts on 1
                ++index24;
                // Convert the 24 bits into 16 bit 565 (Adafruit GFX format)
                switch (index24)
                {
                case 1:
                    in_blue  = in_byte;
                    break;
                case 2:
                    in_green = in_byte;
                    break;
                case 3:
                    in_red   = in_byte;
                    break;
                }
                
                // Every 3rd byte we advance one X
                if (index24 == 3) {
                    if (drawX+1 > bmp.width)
                    {
                        drawX = 0;
                        --drawY;
                    }
                
                    totalDrawPixels++;
                    // Color conversion
                    // Method 1: This works the best for me so far, best variation found
                    //color = 0.3 * in_red + 0.4 * in_green + 0.3 * in_blue;

                    // Method 2: Sent by @vroland https://www.programmersought.com/article/19593930102
                    color = (38 * in_red + 75 * in_green + 15 * in_blue) >> 7;

                    // DEBUG: Turn to true
                    if (false && totalDrawPixels<200) {
                        printf("R:%d G:%d B:%d CALC:%d\n", in_red, in_green, in_blue, color);
                    }
                    
                    epd_draw_pixel(drawX, drawY, color, fb);
                    ++drawX;
                    index24 = 0;
                }
                
            break;

            default:
                ESP_LOGI(TAG, "Unsupported bit-depth mode: %d", bmp.depth); 
            break;
            }

            rowByteCounter++;
            imageBytesRead++;
            forCount++;
        }

        if (bmpDebug)
            printf("Total drawPixel calls: %d\noutX: %d outY: %d\n", totalDrawPixels, drawX, drawY);

        // Hexa dump:
        //ESP_LOG_BUFFER_HEX(TAG, output_buffer, evt->data_len);
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH\nDownload took: %llu ms\nRefresh and go to sleep %d minutes\n", (esp_timer_get_time()-startTime)/1000, DEEPSLEEP_MINUTES_AFTER_RENDER);
        epd_hl_update_screen(&hl, MODE_GC16, 25);
        if (bmpDebug) 
            printf("Free heap after display render: %d\n", xPortGetFreeHeapSize());
        // Go to deepsleep after rendering
        vTaskDelay(2000 / portTICK_PERIOD_MS);
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
     * NOTE: All the configuration parameters for http_client must be spefied either in URL or as host and path parameters.
     * If host and path parameters are not set, query parameter will be ignored. In such cases,
     * query parameter should be specified in URL.
     *
     * If URL as well as host and path parameters are specified, values of host and path will be considered. TESTs:
       http://img.cale.es/bmp/fasani/5e8cc4cf03d81  -> 4 bit 2.7 tests
       http://cale.es/img/test/1.bmp                -> vertical line
       http://cale.es/img/test/circle.bmp           -> Circle test
     */
    // POST Send the IP for logging purpouses
    char post_data[22];
    uint8_t postsize = sizeof(post_data);
    strlcpy(post_data, "ip=", postsize);
    strlcat(post_data, espIpAddress, postsize);

    esp_http_client_config_t config = {
        .url = IMG_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE
        };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    printf("POST data: %s\n", post_data);

    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
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
    //ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));
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
    //memset(&wifi_config, 0, sizeof(wifi_config));

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

void app_main(void)
{
    //Initialize EPDiy component
    epd_init(EPD_OPTIONS_DEFAULT);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    fb = epd_hl_get_framebuffer(&hl);
    epd_clear();

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
    
    // Handle rotation
    epd_set_rotation(EPD_ROT_LANDSCAPE);
    // Show available Dynamic Random Access Memory available after initialization
    printf("Free heap: %d (After epaper instantiation)\n\n", xPortGetFreeHeapSize());
    http_post();
}

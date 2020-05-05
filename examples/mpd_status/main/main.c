/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_types.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

#include "epd_driver.h"
#include "firasans.h"
#include "mpd/client.h"
#include "wifi_config.h"
#include "mpd_image.h"
#include "mpd_info.h"

uint8_t *img_buf;

struct mpd_connection *mpd_conn;

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

static bool got_ip = false;

static album_cover_t *album_cover = NULL;
static mpd_playback_info_t *playback_info = NULL;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI("scan", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    got_ip = true;
  }
}

void show_status(struct mpd_status *status, struct mpd_song *song) {
  memset(img_buf, 255, EPD_WIDTH / 2 * EPD_HEIGHT);

  int cursor_x = 700;
  int cursor_y = 200;
  if (mpd_song_get_tag(song, MPD_TAG_TITLE, 0)) {
    write_string((GFXfont*)&FiraSans, (char*)mpd_song_get_tag(song, MPD_TAG_TITLE, 0), &cursor_x, &cursor_y, img_buf);
  }

  if (mpd_song_get_tag(song, MPD_TAG_ALBUM, 0)) {
    cursor_x = 700;
    cursor_y = 200 + FiraSans.advance_y * 2;
    write_string((GFXfont*)&FiraSans, (char*)mpd_song_get_tag(song, MPD_TAG_ALBUM, 0), &cursor_x,
                 &cursor_y, img_buf);
  }

  if (mpd_song_get_tag(song, MPD_TAG_ARTIST, 0)) {
    cursor_x = 700;
    cursor_y = 200 + FiraSans.advance_y * 3;
    write_string((GFXfont*)&FiraSans, (char*)mpd_song_get_tag(song, MPD_TAG_ARTIST, 0), &cursor_x,
                 &cursor_y, img_buf);
  }

  epd_poweron();
  epd_clear();
  epd_draw_grayscale_image(epd_full_screen(), img_buf);
  epd_poweroff();
}

static void handle_error(struct mpd_connection *c) {
  assert(mpd_connection_get_error(c) != MPD_ERROR_SUCCESS);

  ESP_LOGE("mpd", "%s\n", mpd_connection_get_error_message(c));
  mpd_connection_free(c);
}

static void print_tag(const struct mpd_song *song, enum mpd_tag_type type,
                      const char *label) {
  unsigned i = 0;
  const char *value;

  while ((value = mpd_song_get_tag(song, type, i++)) != NULL)
    printf("%s: %s\n", label, value);
}


void epd_task() {
  epd_init();

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  ESP_ERROR_CHECK(ret);
  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &event_handler, NULL));

  // Initialize and start WiFi
  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASSWORD,
              .scan_method = WIFI_FAST_SCAN,
              .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
              .threshold.rssi = -127,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  while (!got_ip) {
    delay(100);
  }

  mpd_conn = mpd_connection_new("192.168.42.50", 6600, 3000);

  if (mpd_connection_get_error(mpd_conn) != MPD_ERROR_SUCCESS)
    return handle_error(mpd_conn);

  {
    int i;
    for (i = 0; i < 3; i++) {
      printf("version[%i]: %i\n", i,
             mpd_connection_get_server_version(mpd_conn)[i]);
    }
  }

  while (true) {
    mpd_run_idle(mpd_conn);

    if (mpd_connection_get_error(mpd_conn) != MPD_ERROR_SUCCESS ||
        !mpd_response_finish(mpd_conn))
      return handle_error(mpd_conn);

    mpd_playback_info_t* new_info = fetch_playback_info(mpd_conn);
    bool do_update = true;
    if (playback_info != NULL && playback_info != NULL) {
      do_update = memcmp(new_info->hash, playback_info->hash, crypto_generichash_BYTES) != 0;
    }
    if (playback_info) {
      free_playback_info(playback_info);
    }
    playback_info = new_info;

    if (new_info == NULL) {
      ESP_LOGW("main", "update is invalid!");
    }
    if (do_update && new_info) {
      show_status(playback_info->status, playback_info->current_song);

      char *album = (char*)mpd_song_get_tag(playback_info->current_song, MPD_TAG_ALBUM, 0);
      if (album_cover == NULL || album_cover->identifier == NULL || album == NULL || strncmp(album_cover->identifier, album, 128) != 0) {
        if (album_cover) {
          free_album_cover(album_cover);
          album_cover = NULL;
        }
        album_cover = readpicture(mpd_conn, (char*)mpd_song_get_uri(playback_info->current_song), album);
      }

      if (album_cover) {
        Rect_t area = {
          .width = album_cover->width,
          .height = album_cover->height,
          .x = 50,
          .y = (825 - album_cover->height) / 2,
        };
        printf("album cover dimensions: %dx%d\n", album_cover->width, album_cover->height);
        epd_poweron();
        epd_draw_grayscale_image(area, album_cover->data);
        epd_poweroff();
      }
    }

  }
}

void app_main() {
  // copy the image data to SRAM for faster display time
  img_buf = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2,
                                        MALLOC_CAP_SPIRAM);
  if (img_buf == NULL) {
    ESP_LOGE("epd_task", "Could not allocate framebuffer in PSRAM!");
  }

  // memcpy(img_buf, dragon_data, EPD_WIDTH * EPD_HEIGHT / 2);
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}

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
#include "firasans_16.h"
#include "firasans_16_bold.h"
#include "firasans_20.h"
#include "firasans_24.h"
#include "default_album.h"
#include "mpd/client.h"
#include "mpd_image.h"
#include "mpd_info.h"
#include "wifi_config.h"

const int queue_x_start = 900;
const int album_cover_x = 100;
const int album_cover_y = 100;
const int queue_x_end = 1500;
const int queue_y_start = 100;

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

static bool got_ip = false;

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

void show_status(struct mpd_status *status, struct mpd_song *song,
                 uint8_t *img_buf, int album_height) {

  int cursor_x = album_cover_x;
  int cursor_y = album_height + 100;
  if (mpd_song_get_tag(song, MPD_TAG_TITLE, 0)) {
    write_string((GFXfont *)&FiraSans24,
                 (char *)mpd_song_get_tag(song, MPD_TAG_TITLE, 0), &cursor_x,
                 &cursor_y, img_buf);
  }

  if (mpd_song_get_tag(song, MPD_TAG_ALBUM, 0)) {
    cursor_x = album_cover_x;
    cursor_y = album_height + 100 + FiraSans24.advance_y + FiraSans24.advance_y;
    write_string((GFXfont *)&FiraSans20,
                 (char *)mpd_song_get_tag(song, MPD_TAG_ALBUM, 0), &cursor_x,
                 &cursor_y, img_buf);
  }

  if (mpd_song_get_tag(song, MPD_TAG_ARTIST, 0)) {
    cursor_x = album_cover_x;
    cursor_y =
        album_height + 100 + FiraSans24.advance_y + FiraSans20.advance_y * 2;
    write_string((GFXfont *)&FiraSans20,
                 (char *)mpd_song_get_tag(song, MPD_TAG_ARTIST, 0), &cursor_x,
                 &cursor_y, img_buf);
  }
}

void handle_error(struct mpd_connection **c) {
  enum mpd_error err = mpd_connection_get_error(*c);
  if (err == MPD_ERROR_SUCCESS)
    return;

  ESP_LOGE("mpd", "%d %s\n", err, mpd_connection_get_error_message(*c));
  // error is not recoverable
  if (!mpd_connection_clear_error(*c)) {
    mpd_connection_free(*c);
    *c = NULL;
  }
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

  uint8_t *img_buf =
      heap_caps_malloc(EPD_WIDTH / 2 * EPD_HEIGHT, MALLOC_CAP_SPIRAM);

  album_cover_t *album_cover = NULL;
  mpd_playback_info_t *playback_info = NULL;


  memset(img_buf, 255, EPD_WIDTH / 2 * EPD_HEIGHT);

  bool init = true;

  struct mpd_connection *mpd_conn = NULL;
  while (true) {
    // connect / reconnect
    if (mpd_conn == NULL) {
      mpd_conn = mpd_connection_new("192.168.42.50", 6600, 3000);
      handle_error(&mpd_conn);
      int i;
      printf("mpd server version: ");
      for (i = 0; i < 3; i++) {
        printf("%d.", mpd_connection_get_server_version(mpd_conn)[i]);
      }
      printf("\n");
    }

    if (!init) {
      mpd_run_idle(mpd_conn);
      handle_error(&mpd_conn);

      mpd_response_finish(mpd_conn);
      handle_error(&mpd_conn);
    }

    mpd_playback_info_t *new_info = fetch_playback_info(mpd_conn);
    handle_error(&mpd_conn);

    bool do_update = false;

    if (playback_info != NULL && new_info != NULL) {
      do_update = memcmp(new_info->hash, playback_info->hash,
                         crypto_generichash_BYTES) != 0;
    }

    if (playback_info)
      free_playback_info(playback_info);

    playback_info = new_info;

    // no song playing
    if (new_info == NULL && (playback_info != new_info || init)) {
      ESP_LOGW("main", "no song playing");
      epd_poweron();
      epd_draw_image(epd_full_screen(), img_buf, WHITE_ON_WHITE);
      epd_clear_area_cycles(epd_full_screen(), 2, 20);
      memset(img_buf, 255, EPD_WIDTH / 2 * EPD_HEIGHT);
      int x = album_cover_x;
      int y = album_cover_y;
      write_string((GFXfont*)&FiraSans24, "Warteschlange leer.", &x, &y, img_buf);
      epd_draw_grayscale_image(epd_full_screen(), img_buf);
      epd_poweroff();
    }

    if ((do_update || init) && new_info != NULL) {
      epd_poweron();
      epd_draw_image(epd_full_screen(), img_buf, WHITE_ON_WHITE);
      epd_clear_area_cycles(epd_full_screen(), 2, 20);
      epd_poweroff();


      memset(img_buf, 255, EPD_WIDTH / 2 * EPD_HEIGHT);
      mpd_send_list_queue_meta(mpd_conn);
      handle_error(&mpd_conn);

      int queue_y = queue_y_start + FiraSans20.advance_y;
      epd_draw_line(queue_x_start, queue_y_start, queue_x_end, queue_y_start + 1, 0, img_buf);
      struct mpd_entity *entity;
      while ((entity = mpd_recv_entity(mpd_conn)) != NULL) {
        handle_error(&mpd_conn);
        const struct mpd_song *song;
        const struct mpd_directory *dir;
        const struct mpd_playlist *pl;

        switch (mpd_entity_get_type(entity)) {
        case MPD_ENTITY_TYPE_UNKNOWN:
          printf("Unknown type\n");
          break;

        case MPD_ENTITY_TYPE_SONG:
          song = mpd_entity_get_song(entity);
          unsigned duration = mpd_song_get_duration(song);

          GFXfont *font = (GFXfont *)&FiraSans16;
          if (strcmp(mpd_song_get_uri(song),
                     mpd_song_get_uri(playback_info->current_song)) == 0) {
            font = (GFXfont *)&FiraSans16_Bold;
          }

          int queue_x = queue_x_start + 60;
          char timestr[12] = {0};
          snprintf(timestr, 12, "%02d:%02d", duration / 60, duration % 60);
          int t_x = 0, t_y = 0, t_x1, t_y1, t_h, w_time = 0;
          get_text_bounds(font, timestr, &t_x, &t_y, &t_x1, &t_y1, &w_time, &t_h, NULL);

          int w_track = 0;
          char* track = (char *)mpd_song_get_tag(song, MPD_TAG_TRACK, 0);
          get_text_bounds(font, track, &t_x, &t_y, &t_x1, &t_y1, &w_track, &t_h, NULL);

          int timestamp_x = queue_x_end - w_time;
          int timestamp_y = queue_y;
          int track_x = queue_x_start;
          int track_y = queue_y;
          char* title = (char *)mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
          write_string(font, title, &queue_x, &queue_y, img_buf);
          write_string(font, track, &track_x, &track_y, img_buf);
          write_string(font, timestr, &timestamp_x, &timestamp_y, img_buf);
          break;

        case MPD_ENTITY_TYPE_DIRECTORY:
          dir = mpd_entity_get_directory(entity);
          printf("directory: %s\n", mpd_directory_get_path(dir));
          break;

        case MPD_ENTITY_TYPE_PLAYLIST:
          pl = mpd_entity_get_playlist(entity);
          ESP_LOGI("mpd", "playlist: %s", mpd_playlist_get_path(pl));
          break;
        }

        mpd_entity_free(entity);
      }
      epd_draw_line(queue_x_start, queue_y - FiraSans16.advance_y / 2, queue_x_end, queue_y + 1 - FiraSans16.advance_y / 2, 0, img_buf);

      mpd_response_finish(mpd_conn);
      handle_error(&mpd_conn);

      char *album = (char *)mpd_song_get_tag(playback_info->current_song,
                                             MPD_TAG_ALBUM, 0);
      if (album_cover == NULL || album_cover->identifier == NULL ||
          album == NULL || strncmp(album_cover->identifier, album, 128) != 0) {
        if (album_cover) {
          free_album_cover(album_cover);
          album_cover = NULL;
        }
        album_cover = readpicture(
            mpd_conn, (char *)mpd_song_get_uri(playback_info->current_song),
            album);
      }

      int album_height = 700;
      if (album_cover != NULL) {
        Rect_t area = {
            .width = album_cover->width,
            .height = album_cover->height,
            .x = album_cover_x,
            .y = album_cover_y,
        };
        printf("album cover dimensions: %dx%d\n", album_cover->width,
               album_cover->height);
        album_height = album_cover->height;
        epd_copy_to_framebuffer(area, album_cover->data, img_buf);
      } else {
        Rect_t area = {
            .width = DefaultAlbum_width,
            .height = DefaultAlbum_height,
            .x = album_cover_x,
            .y = album_cover_y,
        };
        printf("album cover dimensions: %dx%d\n", DefaultAlbum_width, DefaultAlbum_height);
        album_height = DefaultAlbum_height;
        epd_copy_to_framebuffer(area, (uint8_t*)DefaultAlbum_data, img_buf);
      }

      show_status(playback_info->status, playback_info->current_song, img_buf, album_cover_y + album_height);

      epd_poweron();
      epd_draw_grayscale_image(epd_full_screen(), img_buf);
      epd_poweroff();
    }
    init = false;
  }
}

void app_main() {

  // memcpy(img_buf, dragon_data, EPD_WIDTH * EPD_HEIGHT / 2);
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

  xTaskCreatePinnedToCore(&epd_task, "epd task", 1 << 15, NULL, 2, NULL, 1);
}

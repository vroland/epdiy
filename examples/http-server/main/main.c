#include "server.h"
#include "esp_http_server.h"
#include "epd.h"
#include "esp_heap_caps.h"
#include "settings.h"

#define WRITE_HEADER(req, buffer, name, format, src) \
    sprintf(buffer, format, src);                    \
    ESP_ERROR_CHECK(httpd_resp_set_hdr(req, name, buffer));
static esp_err_t http_index(httpd_req_t* req) {
    EpdData data = n_epd_data();
    char width[20], height[20], temperature[20];
    WRITE_HEADER(req, width, "width", "%d", data.width);
    WRITE_HEADER(req, height, "height", "%d", data.height);
    WRITE_HEADER(req, temperature, "temperature", "%d", data.temperature);
    const char* response = "Hello! Check headers\n";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "200");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t http_clear(httpd_req_t* req) {
    ESP_LOGI(__FUNCTION__, "Clear\n");
    n_epd_clear();
    const char* response = "Cleared\n";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "200");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t http_draw(httpd_req_t* req) {
    // optional headers: x,y. Default to 0
    // required headers: height, width
    // Content should be a stream of special bytes - we're reading 4 bits at a time.
    int x, y, width, height, clear;
    char header[20];
    memset(header, 0, 20);
    if (httpd_req_get_hdr_value_str(req, "clear", header, 20) == ESP_OK) {
        sscanf(header, "%d", &clear);
    } else {
        clear = 0;
    }
    if (httpd_req_get_hdr_value_str(req, "x", header, 20) == ESP_OK) {
        sscanf(header, "%d", &x);
    } else {
        x = 0;
    }
    if (httpd_req_get_hdr_value_str(req, "y", header, 20) == ESP_OK) {
        sscanf(header, "%d", &y);
    } else {
        y = 0;
    }
    if (httpd_req_get_hdr_value_str(req, "width", header, 20) == ESP_OK) {
        sscanf(header, "%d", &width);
    } else {
        char response[60];
        sprintf(response, "Missing header width");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (httpd_req_get_hdr_value_str(req, "height", header, 20) == ESP_OK) {
        sscanf(header, "%d", &height);
    } else {
        char response[60];
        sprintf(response, "Missing header height");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // READING STREAM
    int req_size = req->content_len;
    char* content = (char*)heap_caps_malloc(req_size, MALLOC_CAP_SPIRAM);
    if (content == NULL) {
        char msg[50];
        sprintf(msg, "Failed to allocate %d chars\n", req_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_ERR_INVALID_ARG;
    }
    int current_pos = 0;
    int amount_recieved;
    while ((amount_recieved = httpd_req_recv(req, (content + current_pos), req_size)) > 0) {
        ESP_LOGI(__FUNCTION__, "Read %d bytes\n", amount_recieved);
        current_pos += amount_recieved;
    }
    if (amount_recieved < 0) {
        char msg[50];
        heap_caps_free(content);
        ESP_LOGE(msg, "Failed to read byets. Error code %d\n", amount_recieved);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(__FUNCTION__, "Done reading %d bytes out of %d\n", current_pos, req_size);

    if (clear) {
        n_epd_clear();
    }
    n_epd_draw(((uint8_t*)content), x, y, width, height);
    heap_caps_free(content);

    // Done reading
    char response[100];
    sprintf(
        response, "x %d, y %d, width %d, height %d, byte count %d\n", x, y, width, height, req_size
    );
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "200");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void register_paths(httpd_handle_t server) {
    {
        httpd_uri_t uri
            = { .uri = "/", .method = HTTP_GET, .handler = http_index, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/clear", .method = HTTP_POST, .handler = http_clear, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/draw", .method = HTTP_POST, .handler = http_draw, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
}

void app_main(void) {
    // Initialize NVS, needed for wifi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    httpd_handle_t server = get_server();
    if (server != NULL) {
        register_paths(server);
    }
    n_epd_setup(&SCREEN_MODEL);
}

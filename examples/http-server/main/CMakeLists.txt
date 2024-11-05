set(        
    app_sources "epd.c" "server.c" "main.c"
)

idf_component_register(
        SRCS ${app_sources} 
        REQUIRES epdiy esp_wifi nvs_flash esp_http_server esp_netif
)

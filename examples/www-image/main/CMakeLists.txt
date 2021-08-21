# Select only one app_sources
#set(app_sources "bmp-render.c")
set(app_sources "jpg-render.c")

idf_component_register(SRCS ${app_sources} 
        REQUIRES epd_driver 
        nvs_flash esp-tls esp_http_client
        # Embed the server root certificate into the final binary
        EMBED_TXTFILES ${project_dir}/ssl_cert/server_cert.pem
        )
set(        
    app_sources "jpg-render.c"
    #app_sources "jpgdec-render.cpp"
    )

idf_component_register(SRCS ${app_sources} 
        REQUIRES epd_driver 
        # Add only if you use jpgdec-render.cpp example
        jpegdec
        nvs_flash esp-tls esp_http_client
        # Embed the server root certificate into the final binary
        EMBED_TXTFILES ${project_dir}/ssl_cert/server_cert.pem
        )
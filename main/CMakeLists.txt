# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS
        main.c
        wifi.c
        gemini.c
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        esp_wifi
        nvs_flash
        esp_event
        esp_http_client
        esp-tls
        mbedtls
)

# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS
        main.c
        wifi.c
        gemini.c
        mic.c
        speaker.c
    INCLUDE_DIRS
        "."
    EMBED_TXTFILES
        "system_promt.txt"
    PRIV_REQUIRES
        esp_wifi
        nvs_flash
        esp_event
        esp-tls
        mbedtls
        esp_driver_i2s
        esp_driver_i2c
        esp_driver_gpio
        espressif__cjson
        espressif__esp_websocket_client
)

idf_component_register(SRCS "xiaozhi_chat.c" "main.c" "refill_pump.c" "wifi_module.c" "mqtt_module.c" "ota_module.c" "max98357a.c" "inmp441.c" "ws2812.c" "tts_module.c" "heater.c" "buzzer.c" "water_temp.c" "water_level.c" "water_pump.c" "bsp_adc.c" "TDS.c" "PH.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_adc driver ds18b20 onewire_bus esp_wifi esp_event nvs_flash lwip mqtt esp_driver_i2s esp_driver_rmt esp_partition spi_flash esp_xiaozhi cjson esp_audio_codec app_update esp_http_client)


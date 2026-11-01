# main 组件：源文件与头文件路径
idf_component_register(
    SRCS "main.c"
         "wifi_sta.c"
         "onenet_ota.c"
         "onenet_token.c"
         "xmodem_tx.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif esp_http_client nvs_flash
             json driver mbedtls esp-tls
)

# 覆盖 STM32 当前版本（OneNET 用它判断要不要升级）
# 每次 STM32 升级完成后，把这里改成新的版本号再 idf.py flash
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    STM32_CURRENT_VERSION="1.0.0"
)

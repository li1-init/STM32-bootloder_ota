cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ESP32-S3 版本号（ESP32 自己的固件版本，与 STM32 无关）
set(PROJECT_VER "1.0.0")

project(esp32_ota_bridge)

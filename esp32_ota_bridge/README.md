# ESP32-S3 OneNET OTA Bridge for STM32

把 OneNET 平台的 STM32 固件，通过 ESP32-S3 流式转发给 STM32 Bootloader（XMODEM）。

---

## 一、数据流

```
OneNET 云端 (https://iot-api.heclouds.com/fuse-ota)
   │ ① 上报 STM32 当前版本（HTTP POST /version）
   │ ② 查询升级任务（HTTP GET /check）
   │ ③ 流式下载固件（HTTP GET /{tid}/download）
   ▼
ESP32-S3（本工程）
   │ UART2, 115200, XMODEM 128B CRC
   ▼
STM32 Bootloader 菜单 [1] XMODEM
   → 写 W25Q64 暂存区 → 置 flag → 复位
   → Boot 搬运到 APP 区 → 跳新 APP
```

---

## 二、硬件接线

| ESP32-S3 | STM32F103C8T6 | 说明 |
|---|---|---|
| GPIO17 (TX) | PA3 (USART2_RX) | ESP32 发，STM32 收 |
| GPIO18 (RX) | PA2 (USART2_TX) | STM32 发（ACK），ESP32 收 |
| GND | GND | **必须共地** |
| 3V3 | 3V3 | 电平 3.3V |

---

## 三、使用步骤

### 1. STM32 侧准备

```text
复位 STM32 → 3 秒内串口发 'w' → 进 Bootloader 菜单
按 [7] 进入 USART2 / ESP32 无线 OTA 等待
（此时 STM32 每 800ms 发一个 'C'）
```

### 2. 修改 STM32 当前版本号

在 `main/CMakeLists.txt`：

```cmake
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    STM32_CURRENT_VERSION="1.0.0"      ← 改成 STM32 实际运行的版本
)
```

**这个版本号必须和 OneNET 平台上 STM32 固件包的目标版本不同**，否则云端认为“已是最新”，不会下发。

### 3. 编译烧录 ESP32

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

### 4. OneNET 平台上建 OTA 升级包

1. 控制台 → OTA 升级 → 新建升级包；
2. 上传 **STM32 的 bin**（不是 ESP32 的）；
3. 目标版本填一个比 `STM32_CURRENT_VERSION` 大的版本，如 `1.1.0`；
4. 选择设备 `esp32led02`。

### 5. 上电

ESP32 日志：

```text
I (xxx) ota_bridge: ===== OneNET OTA bridge start =====
I (xxx) ota_bridge: STM32 current version: 1.0.0
I (xxx) wifi: got ip: 192.168.x.x
I (xxx) onenet_ota: report version 1.0.0 -> HTTP 200
I (xxx) onenet_ota: check task -> HTTP 200, body={"code":0,"data":{"tid":123,"target":"1.1.0",...}}
I (xxx) ota_bridge: new version available: 1.1.0 (tid=123, size=8616)
I (xxx) ota_bridge: >>> please put STM32 Bootloader into menu [7] wireless OTA <<<
I (xxx) xmodem_tx: waiting for 'C' from STM32 ...
I (xxx) xmodem_tx: got 'C', start sending
I (xxx) onenet_ota: downloaded 8192 bytes
I (xxx) xmodem_tx: EOT acknowledged, transfer done (68 blocks)
I (xxx) ota_bridge: ===== transfer done, 8616 bytes =====
```

STM32 串口：

```text
XMODEM: ready (128B CRC), send file now
  blk=  1  128 bytes
  ...
  blk= 68  8704 bytes
XMODEM: done, len=8616 crc32=0x...
Reset in 1s, Bootloader will copy staging to APP
Boot: upgrade done, flag cleared
########## APP v1.1.0 (SLOT A) ##########
```

---

## 四、关键点

| 点 | 说明 |
|---|---|
| **流式转发** | ESP32 不把固件存自己 Flash，收到一块发一块，RAM 只占 1KB |
| **停等流控** | 每包等 STM32 的 ACK 再发下一包，天然不会淹没 STM32 |
| **重试** | 单包最多重试 5 次，超时 1 秒 |
| **取消** | 下载失败发 `CAN`，STM32 退出 XMODEM |
| **授权** | 用设备级 token（`dev_token_generate`），从旧工程验证过 |
| **HTTPS** | 用 `esp_crt_bundle`，不用自己烧证书 |

---

## 五、文件说明

| 文件 | 作用 |
|---|---|
| `main/main.c` | 流程编排：WiFi → 上报 → 查任务 → 下载 → XMODEM |
| `main/wifi_sta.c` | WiFi station 连接 |
| `main/onenet_ota.c` | OneNET HTTP API（上报/查任务/流式下载/上报状态） |
| `main/xmodem_tx.c` | XMODEM 发送（128B CRC，等 ACK，重试） |
| `main/onenet_token.c` | OneNET 设备 token 生成（SHA256 + Base64，从旧工程复制） |
| `main/app_secrets.h` | 产品 ID / 密钥 / 设备名 / WiFi |

---

## 六、ESP32-S3 引脚

```c
/* main/xmodem_tx.c */
#define XM_UART_NUM      UART_NUM_2
#define XM_UART_TX_PIN   17
#define XM_UART_RX_PIN   18
#define XM_UART_BAUD     115200
```

换引脚只改这里。

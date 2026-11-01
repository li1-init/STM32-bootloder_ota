# 模块架构说明

## 1. 设计目标

本工程按以下规则组织：

1. **单一事实来源**
   - Flash 地址只在 `common/partition_layout.h` 定义。
   - W25Q64 OTA 记录只在 `common/ota_info.*` 实现。
   - APP 更新状态只在 `common/app_update.*` 实现。
2. **层次分离**
   - 驱动层只负责硬件读写。
   - 协议层只负责帧、ACK/NAK、CRC、状态机。
   - 业务层负责升级流程、分区选择和错误处理。
   - `main()` 只做模块编排。
3. **工程隔离**
   - Bootloader、Factory、APP1 各自保留独立启动文件和 Keil 工程。
   - 各工程只编译自己需要的公共模块。
4. **无隐式共享状态**
   - RAM 缓冲区归属明确。
   - 中断服务程序只把数据推入对应 FIFO。
   - 协议解析在主循环上下文中完成。

## 2. 分层结构

```text
┌──────────────────────────────────────────────┐
│ 应用层                                       │
│ APP1 main / Factory main / ESP32 main        │
├──────────────────────────────────────────────┤
│ 业务层                                       │
│ bootloader.c / usart_boot_cmd.c              │
│ factory_xmodem.c / ota_bridge main.c         │
├──────────────────────────────────────────────┤
│ 协议层                                       │
│ xmodem.c / xmodem_tx.c / onenet_ota.c        │
├──────────────────────────────────────────────┤
│ 服务层                                       │
│ ota_info.c / app_update.c / CRC32            │
├──────────────────────────────────────────────┤
│ 驱动层                                       │
│ my_usart.c / my_usart2.c / MySPI.c           │
│ W25Q64.c / stm32f10x_flash.c                 │
├──────────────────────────────────────────────┤
│ BSP / 启动层                                 │
│ struct_init.c / system_stm32f10x.c           │
│ startup_stm32f10x_md.s                       │
└──────────────────────────────────────────────┘
```

## 3. 公共模块

### `common/partition_layout.h`

职责：

- Flash/SRAM 分区唯一来源
- 保持 Bootloader、Factory、APP1 地址一致
- 提供新命名 `PARTITION_*`
- 保留旧命名 `stm32_a_saddr` 等兼容现有代码

不允许在其他头文件中重新定义分区地址。

### `common/app_update.c/.h`

职责：

- 在 APP2 首 1KB 页写入“APP 更新中”标志
- Bootloader 检测标志后禁止跳 APP1
- Factory 完整写入并 CRC 校验通过后清除标志

接口：

```c
int app_update_pending(void);
int app_update_begin(void);
int app_update_finish(void);
```

状态约定：

```text
标志页为空         -> 无更新进行中
标志页 = APPU 魔数 -> 更新未提交，Boot 必须进入 Factory
```

### `common/ota_info.c/.h`

职责：

- W25Q64 主/备 OTA 记录
- 序列号选择
- CRC32-IEEE
- 记录合法性检查
- W25Q64 非对齐写自测

接口：

```c
int ota_info_load(ota_info_t *info);
int ota_info_save(const ota_info_t *info);
void ota_info_clear(ota_info_t *info);
void ota_info_dump(const ota_info_t *info);
int ota_info_format(void);
uint32_t ota_crc32(const void *buf, uint32_t len);
```

### `common/drivers/MySPI.*`

职责：

- GPIO 模拟 SPI Mode 0
- 只负责单字节收发和 CS 控制
- 不包含 W25Q64 指令和重试逻辑

### `common/drivers/W25Q64.*`

职责：

- JEDEC ID
- 状态寄存器
- 写使能 WEL 检查
- 4KB 擦除与全 0xFF 回读
- 256B 页编程与跨页拆分
- 任意长度读写
- 回读校验

上层不得直接调用裸 `PageProgram` / `SectorErase`，应使用安全包装接口。

## 4. Bootloader 模块

| 模块 | 职责 |
|---|---|
| `user/main.c` | 初始化、菜单入口、OTA 处理、分区选择 |
| `mycode/bootloader.c` | 镜像有效性、跳转、W25Q64 → APP1 搬运 |
| `mycode/usart_boot_cmd.c` | 串口菜单与命令分派 |
| `mycode/xmodem.c` | XMODEM-128/1K CRC 接收状态机 |
| `mycode/bootloader.h` | Bootloader 业务接口 |
| `mydrive/my_usart.c` | USART1 + DMA + IDLE，PC 串口与菜单 |
| `mydrive/my_usart2.c` | USART2 + DMA + IDLE，ESP32 通道 |
| `mycode/struct_init.c` | GPIO、TIM3 毫秒时基 |

### Boot 启动流程

```text
SystemInit
  -> all_init()
  -> W25Q64_Init()
  -> 3 秒救援窗口
  -> 菜单（可选）
  -> boot_main_process()
       -> 检查 W25Q64 OTA 记录
       -> 校验暂存区 CRC32
       -> 设置 APP 更新标志
       -> 擦除 APP1
       -> 搬运
       -> 整体 CRC32
       -> 清除 APP 更新标志
  -> app_update_pending() ?
       yes -> 跳过 APP1，进入 Factory
       no  -> app_is_valid(APP1) ? 跳 APP1 : 跳 Factory
```

### APP1 有效性

`app_is_valid()` 检查：

- MSP 位于 SRAM
- MSP 四字节对齐
- Reset_Handler 位于分区内
- Thumb 位为 1

它只能说明入口“像程序”，不能说明整包完整。整包完整性由 OTA 流程和更新标志保证。

## 5. Factory 模块

| 模块 | 职责 |
|---|---|
| `user/main.c` | 救砖菜单和复位流程 |
| `mycode/factory_xmodem.c` | XMODEM 接收并直接写 APP1 |
| `mydrive/my_usart.c` | USART1 PC 通道 |
| `mycode/struct_init.c` | GPIO、TIM3 毫秒时基 |

Factory 不依赖 W25Q64：

```text
进入 XMODEM
  -> app_update_begin()
  -> 擦除 APP1 第一页
  -> 接收数据
  -> 每 1KB 擦除并写入 APP1
  -> EOT
  -> 去掉 0x1A 填充
  -> APP1 整体 CRC32 回读
  -> 通过后 app_update_finish()
```

任何中断、断电或 CRC 失败都会保留更新标志，下一次 Boot 强制回到 Factory。

## 6. APP1 模块

| 模块 | 职责 |
|---|---|
| `user/main.c` | 业务主循环、版本输出、LED 心跳 |
| `mydrive/my_usart.c` | USART1 通信 |
| `mycode/struct_init.c` | GPIO、TIM3 毫秒时基 |
| `common/ota_info.*` | 只读 OTA 信息 |
| `common/drivers/*` | W25Q64 只读/自测 |

APP1 不包含升级协议，避免业务程序与 Bootloader 相互依赖。

`app_test_v110` 是 v1.1.0 测试/升级包工程，使用同一套公共分区、OTA 信息和 W25Q64 驱动，但保留独立的版本号与 LED 行为。

## 7. ESP32-S3 模块

| 模块 | 职责 |
|---|---|
| `main.c` | 流程编排：WiFi → 上报 → 查任务 → 下载 → XMODEM |
| `wifi_sta.c` | WiFi station |
| `onenet_ota.c` | OneNET HTTP API 与流式下载 |
| `onenet_token.c` | 设备 Token |
| `xmodem_tx.c` | XMODEM-128 CRC 发送 |
| `app_secrets.h` | 产品、设备、WiFi 凭证 |

### 无线升级流程

```text
上报 STM32 当前版本
  -> 查询 OneNET 任务
  -> 任务状态必须是可下载状态
  -> 等待 STM32 的 'C'
  -> 流式 HTTP 读取
  -> 每块数据 XMODEM 发送
  -> 每包等待 ACK
  -> EOT
  -> 校验 sent == cloud size
  -> 上报完成
```

注意：

- 只能有一个下载数据入口，不能同时用 HTTP 事件回调和手工 read。
- 同一个已完成 OneNET 任务不能重复下载，需要新建任务。
- ESP 的 `STM32_CURRENT_VERSION` 必须和 STM32 实际版本保持一致。

## 8. XMODEM 设计

### 接收端

支持：

- `SOH` 128B 数据包
- `STX` 1024B 数据包
- CRC16-CCITT，初值 0x0000
- 块号与反码检查
- 重复上一包只 ACK，不重复写
- 超时 NAK
- 包间不跨越 W25Q64 页编程边界

### 发送端

- 128B 数据包
- 每包等待 ACK
- 单包最多重试 5 次
- CAN 取消
- 最后不足 128B 用 0x1A 填充
- 实际发送量与云端声明大小必须一致

## 9. RAM 与缓冲区归属

| 缓冲区 | 所属模块 | 用途 |
|---|---|---|
| `usart_rx_buf` | `my_usart.c` | USART1 DMA 帧缓冲 |
| `usart_tx_buf` | `my_usart.c` | 格式化串口输出 |
| `rx_ota` | `my_usart.c` | USART1 XMODEM 字节流 |
| `s_rx_buf` | `my_usart2.c` | USART2 环形缓冲 |
| `s_dma_buf` | `my_usart2.c` | USART2 DMA 单帧缓冲 |
| `data[]` | `xmodem.c` | XMODEM 包数据 |
| `info` | `xmodem.c` | OTA 信息暂存 |
| APP 更新标志页 | `app_update.c` | 片内 Flash 状态 |

中断服务程序只做：

1. 清 IDLE
2. 计算 DMA 剩余长度
3. 拷贝到环形缓冲
4. 重装 DMA

不在中断中解析协议或写 Flash。

## 10. 错误处理约定

| 错误 | 处理 |
|---|---|
| W25Q64 ID 错误 | 终止 OTA，保持 APP 不变 |
| W25Q64 擦除失败 | 返回错误，不继续写 |
| XMODEM CRC 错误 | NAK，等待重发 |
| XMODEM 重复块 | ACK，跳过写入 |
| XMODEM 取消 | 返回失败 |
| APP1 CRC 错误 | 不提交更新 |
| Factory 中途断电 | 更新标志保持，Boot 进 Factory |
| ESP 下载长度错误 | 发送 CAN，不上报成功 |

## 11. 构建产物

```text
bootloader/Objects/template.hex
factory/Objects/template.hex
app1/Objects/template.hex
app1/Objects/app_v100.bin
app_test_v110/Objects/app_v110.bin
esp32_ota_bridge/build/esp32_ota_bridge.bin
```

## 12. 后续可扩展方向

1. 使用 APP2 真正实现 A/B 双分区。
2. 将 APP 版本从 ESP32 编译常量改为 STM32 串口上报。
3. 增加 OTA 任务状态机和断点续传。
4. 增加固件签名验证。
5. 把 Keil 工程与标准库副本进一步收敛为统一工作区。
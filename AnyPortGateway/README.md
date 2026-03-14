## AnyPort ESP32-C3 网关与模拟器固件（Arduino 工程说明）

本目录包含 AnyPort 项目的 ESP32-C3 固件工程，当前版本：**v1.2.0**。

固件支持三种独立的工作模式：
1. **MQTT 网关模式**：将现场 Modbus RTU/TCP 设备的数据转发至 MQTT Broker，支持远程双向通讯。
2. **从站模拟器模式**：模拟标准的 Modbus RTU/TCP 从站设备，支持寄存器自定义、数据类型转换及动态数值模拟。
3. **USB 转 RS485 透传模式 (New)**：将 ESP32-C3 作为 USB-to-RS485 高速桥接器，支持上位机软件（如 Modbus Poll）通过 USB 直接调试 485 设备。

---

### 1. 开发环境要求

- **IDE**：Arduino IDE 2.x 或 VS Code + PlatformIO。
- **开发板核心**：ESP32 开发板支持包（Espressif Systems）。
  - 在 Arduino IDE 中：安装 `esp32 by Espressif Systems 2.0.14`。
- **推荐板型**：`ESP32C3 Dev Module`。
- **关键设置**：编译时 **"USB CDC On Boot"** 建议设为 **"disabled"**。

---

### 2. 需要安装的 Arduino 库

请通过库管理器安装以下依赖：

1. **Ethernet** (作者 Arduino/Paul Stoffregen)：用于 W5500 以太网通讯（专用于 Modbus TCP 静态连接）。
2. **ArduinoJson** (作者 Benoit Blanchon)：用于 JSON 消息的解析与序列化。
3. **MQTT** (作者 Joel Gaehwiler)：轻量级 MQTT 客户端实现。

---

### 3. 工程结构

- `AnyPortGateway.ino`：主程序入口。
- `Config.h`：核心配置文件，包含引脚、全域变量定义及初始化流控。
- `Globals.h`：底层常量、引脚定义及固件版本号。
- `TransparentHandler.h`：**Mode 3 核心引擎**，实现 USB 与 RS485 的双向高速透传。
- `NetworkManager.h`：WiFi (STA/AP)、NTP 时间同步、mDNS 发现服务。
- `MqttHandler.h`：MQTT 连线管理与重连安全阀逻辑。
- `ModbusHandler.h`：Modbus 协议转发逻辑与 CRC 校验。
- `WebHandler.h`：内置 Web 控制面板，包含所有模式切换与参数配置界面。
- `SimulatorCore.h` / `SimulatorWeb.h`：从站模拟器核心逻辑与 Web API。

---

### 4. 主要功能特性

#### v1.2.0 (当前版本)
- **USB 转 RS485 透传**：针对 ESP32-C3 原生 USB 优化。支持在 Web 端手动配置 RS485 侧的波特率、校验位等参数。
- **mDNS 域名访问**：支持通过 `anyport.local` 直接访问 Web 后台，无需记忆动态 IP。
- **网络稳定性优化**：WiFi 掉线自动重连，MQTT 失败 3 次自动重试锁死保护（确保 Web 访问始终可用）。

#### v1.1.0
- **从站模拟器**：支持 0x/1x/3x/4x 区域，支持 ABCD/DCBA/BADC/CDAB 四种字节序转换及 Float/Int32/String 等数据类型。
- **动态模拟**：模拟寄存器数值随机跳动、累加或递减。

---

### 5. 引脚定义（ESP32-C3）

| 外设 | 引脚功能 | GPIO | 说明 |
| :--- | :--- | :--- | :--- |
| **W5500** | CS / SCK / MISO / MOSI | 7 / 6 / 5 / 4 | SPI 总线连接 |
|  | RST / INT | 1 / 3 | 复位与中断 |
| **RS485** | TXD / RXD | 10 / 2 | 对应硬件 UART1 |

---

### 6. 快速开始

1. **编译烧录**：将工程烧录至 ESP32-C3 开发板。
2. **连接网络**：
   - 第一次使用会开启 AP 热点 `anyport` (密码 `12345678`)。
   - 配置 WiFi 联网后，直接访问 `http://anyport.local` 进入控制面板。
3. **模式切换**：
   - 进入控制面板，在“基础配置”中切换模式（网关/模拟器/透传）。
   - **进入透传模式**：保存后，ESP32 充当“智能调试线”，上位机软件选择对应的串口号即可通讯。

---

### 7. 注意事项

- **透传参数同步**：由于 ESP32-C3 内部 USB 硬件限制，它是无法感知上位机软件（如 Modbus Poll）里点击的波特率修改的。**必须在 AnyPort 的 Web 页面上设置 RS485 参数**。
- **供电**：W5500 建议连接 5V 供电。
- **串口隔离**：进入透传模式后，系统会自动关闭所有调试 Log 输出，防止干扰原始 RS485 通信。

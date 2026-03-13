## AnyPort ESP32-C3 网关与模拟器固件（Arduino 工程说明）

本目录包含 AnyPort 项目的 ESP32-C3 固件工程，当前版本：**v1.1.0**。

固件支持两种工作模式：
1. **MQTT 网关模式**：将现场 Modbus RTU/TCP 设备的数据转发至 MQTT Broker，支持远程双向通讯。
2. **从站模拟器模式**：模拟标准的 Modbus RTU/TCP 从站设备，支持寄存器自定义、数据类型转换及动态数值模拟（用于测试）。

---

### 1. 开发环境要求

- **IDE**：Arduino IDE 2.x 或 VS Code + PlatformIO。
- **开发板核心**：ESP32 开发板支持包（Espressif Systems）。
  - 在 Arduino IDE 中：
    - 安装 `esp32 by Espressif Systems 2.0.14` 支持包。
- **推荐板型**：`ESP32C3 Dev Module`。

---

### 2. 需要安装的 Arduino 库

请通过库管理器安装以下依赖：

1. **Ethernet** (作者 Arduino/Paul Stoffregen)：用于 W5500 以太网通讯。
2. **ArduinoJson** (作者 Benoit Blanchon)：用于 JSON 消息的解析与序列化。
3. **MQTT** (作者 Joel Gaehwiler)：轻量级 MQTT 客户端实现。

---

### 3. 工程结构

- `AnyPortGateway.ino`：主程序入口，包含 `setup()` 和 `loop()`。
- `Config.h`：核心配置文件，定义全局变量、初始化顺序及 NVS 加载逻辑。
- `Globals.h`：引脚定义、全局常量及 `extern` 变量声明。
- `NetworkManager.h`：WiFi (STA/AP)、W5500 以太网、NTP 时间同步逻辑。
- `MqttHandler.h`：MQTT 连接管理、状态发布（心跳）及主题构建。
- `ModbusHandler.h`：Modbus 协议转发逻辑（RTU/TCP）及 CRC 计算。
- `WebHandler.h`：内置 Web 服务器路由、配置面板 HTML 页面。
- `SimulatorCore.h`：从站模拟器核心（寄存器池、字节序处理、动态模拟逻辑）。
- `SimulatorWeb.h`：模拟器专属 Web API（寄存器读写、模拟器参数配置）。

---

### 4. 主要功能特性

#### v1.0.0 (网关功能)
- **协议转换**：Modbus RTU (RS485) / Modbus TCP (以太网) ↔ MQTT。
- **网络冗余**：支持 WiFi (STA/AP) 与 W5500 以太网自动初始化。
- **Web 配置**：支持通过浏览器修改网络设置、MQTT Broker 信息。

#### v1.1.0 (模拟器与增强)
- **从站模拟器**：支持模拟 0x/1x/3x/4x 区域，自定义地址、名称、数据类型（Float, Int32 等）及字节序（ABCD, DCBA 等）。
- **动态模拟**：支持对寄存器值进行随机偏移、自动累加或递减，以便于前端压力测试。
- **版本可视化**：Web 控制面板直观显示当前固件版本。

---

### 5. 引脚定义（ESP32-C3）

| 外设 | 引脚功能 | GPIO | 说明 |
| :--- | :--- | :--- | :--- |
| **W5500** | CS / SCK / MISO / MOSI | 7 / 6 / 5 / 4 | SPI 总线连接 |
|  | RST / INT | 1 / 3 | 复位与中断 |
| **RS485** | TXD / RXD | 10 / 2 | 对应硬件 UART1 |

---

### 6. 快速开始

1. **编译烧录**：使用 Arduino IDE 打开 `AnyPortGateway` 文件夹，编译并烧录至 ESP32-C3。
2. **连接热点**：若未配置 WiFi，搜索并连接热点 `anyport`（密码 `12345678`）。
3. **访问配置页**：在浏览器打开 `192.168.4.1` 访问固件控制面板。
4. **切换模式**：在“基础配置”中选择工作模式为“MQTT 网关”或“从站模拟器”，保存后设备将自动重启生效。

---

### 7. 注意事项

- **供电**：W5500 建议连接 5V 供电，以防 3.3V LDO 电流不足导致掉线。
- **MAC 地址**：固件自动读取 ESP32 的出厂 MAC 地址用于以太网通讯，无需手动配置。
- **自动流控**：RS485 假定使用自动收发切换模块，无需手动控制 DE 引脚。

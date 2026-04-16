# AnyPort ESP32 Gateway Firmware

AnyPort 智能网关系统固件，基于 **ESP32-C3/S3** 架构。本仓库提供了工业级网关的核心逻辑、协议转换、边缘侧 Web 配置界面及高可用的远程更新 (OTA) 系统。

---

## 🏗️ 项目架构 (Architecture)

固件采用模块化、层次化的设计，旨在提供高度的可扩展性和稳定性：

### 1. 核心框架层
- **`AnyPortGateway.ino`**: 极简主入口，负责系统引导。
- **`Config.h` & `Globals.h`**: 全局配置中心，管理内存对象映射、引脚定义、持久化 NVS 配置加载。

### 2. 协议与业务模块 (Handlers)
- **`NetworkManager.h`**: 负责 WiFi 与 W5500 以太网的自动切换与链路保持。
- **`ModbusHandler.h`**: 提供 Modbus TCP/RTU 指令转发、TCP 连接复用及从站扫描能力。
- **`MqttHandler.h`**: 处理与云端双向通信，支持基于 Topic 的远程控制和状态上报。
- **`BridgeHandler.h` / `EthWifiHandler.h`**: 实现跨物理介质与协议的透明转发。
- **`TransparentHandler.h`**: 高效的串口透传逻辑，支持低延迟数据流转。

### 3. 边缘与运维模块
- **`WebHandler.h`**: 内置 Web 控制面板，提供设备状态监控及动态配置。
- **`SimulatorCore.h` / `SimulatorWeb.h`**: 工业从站模拟器，简化现场调试。
- **`OtaHandler.h`**: 
  - **三段式校验**: 检查版本 -> 签名预检 -> 带 Token 跨域下载。
  - **安全加固**: 基于 HMAC-SHA256 签名的预签名 URL。
  - **防故障模式**: 支持 A/B 分区回滚与维护模式。

---

## 🚀 核心功能 (Features)

- **多模工作流**:
  - **MQTT 网关模式 (0)**: 连接工业现场总线与物联网平台。
  - **模拟器模式 (1)**: 模拟 Modbus 从站逻辑，支持 Web 实时调试。
  - **透传模式 (2)**: 纯粹的串口转网络透明传输。
  - **协议互转模式 (3)**: 执行 Modbus TCP 与 RTU 之间的标准协议转换。
  - **以太网-WiFi 桥接 (5)**: 专门设计的流量中转，用于跨网段接入。
- **网络鲁棒性**: 
  - 支持 **W5500 硬以太网** 与 **内部 WiFi** 同时在线。
  - 自动检测链路状态，OTA 升级流量强制走 WiFi 通道确保更新可靠性。
- **安全升级**: 集成生产级远程升级系统，支持版本自动检测 (24h 间隔) 与双固件分区。

---

## 🔌 硬件资源分配 (Pinout)

默认适配 **ESP32-C3** 核心板：

| 功能模块 | 引脚 / 协议 | ESP32 引脚 | 说明 |
| :--- | :--- | :--- | :--- |
| **W5500 (SPI)** | SCK / MISO / MOSI | GPIO 6 / 5 / 4 | 高速以太网通信 |
| | CS / RST | GPIO 7 / 1 | 硬件复位确保冷启动稳定性 |
| **RS485 (UART)** | RX / TX | GPIO 2 / 10 | 建议配合自动收发流控模块 |
| **Debug** | Serial | GPIO (Default) | 波特率 115200 |

---

## 🛠️ 快速开始

### 1. 依赖库 (Dependencies)
请通过 Arduino Library Manager 或 PlatformIO 安装以下库：
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)**: v6.x，用于复杂的 JSON 消息解析与生成。
- **[PubSubClient](https://github.com/knolleary/pubsubclient)**: (或 `MQTT` 库)，用于 MQTT 协议接入。
- **[Ethernet](https://github.com/arduino-libraries/Ethernet)**: 适配 W5500 模块。
- **[HTTPClient](https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient)**: ESP32 系统自带库，用于 API 与镜像获取。

### 2. 编译配置
- 修改 `OtaHandler.h` 中的 `OTA_API_BASE` 指向你的后端 API。
- 在 `Globals.h` 中根据需求调整默认的波特率、MQTT 证书信息。

---

## 🤝 关联项目
- **[AnyPort Simulator UI](https://github.com/hsieh19/anyport)**: 配套的前端 Web 控制台。

## 📜 许可证
© 2026 Hotwon-CD2-Hsieh / AnyPort Project.

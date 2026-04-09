# AnyPort ESP32 Gateway Firmware

AnyPort 智能网关的系统固件，基于 ESP32-C3/S3 架构。本仓库提供了网关的核心逻辑、协议转换、以及高可用的远程更新系统。

## 🚀 核心功能
- **多模式运行**：支持 MQTT 网关、从站模拟器、USB 透传以及协议互转（Modbus TCP <-> RTU）。
- **安全 OTA**：内置生产级远程升级系统，支持基于 HMAC-SHA256 签名的预签名 URL 下载。
- **自动巡检**：设备启动 30s 后及每隔 24h 自动检查云端版本，发现更新时 Web UI 自动弹出提醒。
- **网络自适应**：支持 WiFi 与 W5500 以太网共存，OTA 流量强制走 WiFi 通道确保云端可达性。
- **独立维护模式**：新增维护模式（模式 4），在升级期间关闭所有非必要业务逻辑，确保烧录环境纯净稳定。

## 🔌 硬件引脚定义 (Pinout)
本固件针对 ESP32-C3/S3 开发，默认引脚配置如下：

### **以太网模块 (W5500)**
| 功能 | ESP32 引脚 | 说明 |
| :--- | :--- | :--- |
| **SCK** | GPIO 6 | SPI 时钟 |
| **MISO** | GPIO 5 | SPI 数据输入 |
| **MOSI** | GPIO 4 | SPI 数据输出 |
| **CS** | GPIO 7 | 片选 |
| **RST** | GPIO 1 | 复位 (硬复位确保稳定性) |
| **INT** | GPIO 3 | 中断 (可选) |

### **RS485 通信 (UART)**
| 功能 | ESP32 引脚 | 对应模块引脚 |
| :--- | :--- | :--- |
| **RX** | GPIO 2 | 模块 RO (Receive Output) |
| **TX** | GPIO 10 | 模块 DI (Driver Input) |

> [!TIP]
> 建议在 RS485 模块的 DE/RE 引脚上使用自动收发电路，或者将它们连接到 GPIO 以进行手动流控（当前固件默认支持自动收发逻辑模块）。

## 🛠 快速开始
1. **编译环境**：使用 Arduino IDE 2.x 或 PlatformIO。
2. **依赖库**：
   - `ArduinoJson`
   - `PubSubClient`
   - `HTTPClient`
   - `HTTPUpdate`
3. **配置文件**：
   - 在 `OtaHandler.h` 中配置你的 OTA 服务器地址 (`OTA_API_BASE`)。
   - 在 `Globals.h` 中配置你的默认 MQTT Broker 地址。
   - 在加密密钥环境变量中设置与 Cloudflare Worker 一致的 `SEC_KEY`。

## 🔐 OTA 安全机制
AnyPort 采用“三段式”下载流程以确保固件安全：
1. **版本检查**：请求 `/check` 获取最新版本号与 Changelog。
2. **预检与签名**：请求 `/get-link`。Worker 验证 R2 存储中文件存在性后，返回一个 10 分钟有效的加密签名链接。
3. **安全下载**：ESP32 使用签名链接从 `/download` 接口拉取固件流并执行 A/B 分区切换。

## 🤝 关联项目
- [AnyPort Simulator UI](https://github.com/hsieh19/anyport)：配套的前端调试与配置工具。

## 📜 许可证
© 2026 Hotwon-CD2-Hsieh / AnyPort Project.

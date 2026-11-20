# AnyPort ESP32 Gateway Firmware (v2.0)

AnyPort 智能工业网关系统固件 v2.0，基于 **ESP32-C3 (`Dual485-Controller-V1`)** 硬件平台构建。本仓库提供工业级多模协议转换、被动总线质量诊断、双主站防碰撞仲裁中继、RESTful JSON API 及高可用双分区远程更新 (OTA) 固件系统。

---

## 🏗️ 项目架构 (Clean Architecture)

固件严格遵循 **整洁架构 (Clean Architecture)** 与 **SOLID** 设计原则，采用标准 `.h` / `.cpp` 独立编译单元实施五层单向依赖解耦（源码位于 `AnyPortGateway/src/`）：

### 1. 领域与协议核心层 (`src/domain/`)
- **`AppTypes.h`**: 硬件引脚常量、7 大工作模式枚举、全量配置结构体、全局遥测指标结构体 (`SystemTelemetry`) 及统一日志宏。
- **`ProtocolUtils.h / .cpp`**: 统一 Modbus CRC16 校验、Hex/Bytes 编解码、IPv4 地址解析与串口配置掩码计算。
- **`SimulatorDomain.h / .cpp`**: 从站模拟器寄存器池（20000 寄存器/线圈）、变量映射、32 位字节序转换 (`ABCD/DCBA/BADC/CDAB`)、动态变量生成及 Modbus PDU 引擎。
- **`BusMonitorDomain.h / .cpp`**: 总线被动嗅探 100 条环形报文缓冲区、主从请求-应答 (`Req/Resp`) 自动配对状态机、从站响应耗时 (RTT) 与丢包/异常码/CRC 误码质量统计。

### 2. 基础设施与硬件驱动层 (`src/infra/`)
- **`ConfigStore.h / .cpp`**: NVS (`Preferences`) 持久化配置中心，统一收口系统配置、全局双路 RS485 参数池、工作模式、模拟器寄存器与 OTA 版本快照的读写。
- **`Rs485Driver.h / .cpp`**: 双路 RS485 (`485-1` / `485-2`) 硬件串口驱动与通道抽象。
- **`NetworkManager.h / .cpp`**: 负责 WiFi (STA/AP 自动回退) 与 W5500 硬以太网多轮 PHY 自适应建链。
- **`OtaService.h / .cpp`**: 远程固件版本检查、URL 预检升级与 ESP32 原生 A/B 双分区安全回滚服务。

### 3. 应用业务层 (`src/modes/`)
所有工作模式统一实现 `IWorkMode` (`begin()` / `loop()`) 生命周期契约，采用编译期静态实例注册（零堆碎片）：
- **`GatewayMode.h / .cpp` (Mode 0)**: MQTT 物联网云网关，处理云端双向通信、心跳上报、Modbus TCP/RTU 远程指令转发及 TCP Ping 诊断。
- **`SimulatorMode.h / .cpp` (Mode 1)**: 工业 Modbus 从站模拟器（支持 RTU 与双网口 TCP 并发访问）。
- **`TransparentMode.h / .cpp` (Mode 2)**: 高吞吐量、低延迟的 USB-CDC 转 RS485 透明传输。
- **`BridgeMode.h / .cpp` (Mode 3)**: 标准 Modbus TCP ↔ RTU 双向协议互转（支持总线级与单设备固定从站 ID）。
- **`BusMonitorMode.h / .cpp` (Mode 4)**: RS485 总线被动监听与通讯质量诊断（纯高阻态只听不发、主从应答配对与 RTT/超时/CRC 误码实时统计）。
- **`EthWifiBridgeMode.h / .cpp` (Mode 5)**: 以太网与 WiFi 跨网段流量桥接（支持 `WiFi → W5500` 与 `W5500 → WiFi` 双向 × `TCP / UDP` 双协议透明转发）。
- **`DualMasterMode.h / .cpp` (Mode 6)**: 双主站防碰撞中继网关（硬件双 RS485 物理隔离 + WiFi TCP 主站互斥仲裁，原生兼容 AnyControl 平台在线热重载）。

### 4. 接口适配层与应用内核 (`src/api/` & `src/app/`)
- **`HttpApiServer.h / .cpp`**: RESTful JSON API 服务器基础设施、CORS / PNA 跨域预检与参数校验器。
- **`SystemController.h / .cpp`**: 极简应急配网页 (`GET /`)、全局系统状态遥测 (`/api/sys/status`)、全量配置读写 (`/api/config`)、双主站热重载 (`/api/gateway/config`) 与 OTA 升级接口 (`/api/ota/*`)。
- **`SimulatorController.h / .cpp`**: 从站模拟器寄存器 (`/api/registers`)、参数 (`/api/simConfig`) 与总线监听质量诊断流式接口 (`/api/monitor*`)。
- **`AppKernel.h / .cpp` & `AnyPortGateway.ino`**: 应用引导内核与多态主循环调度。

---

## 🚀 核心功能 (Features)

- **7 大工业工作模式**:
  - **0 - MQTT 网关模式**: 连接工业现场总线与物联网平台，支持提取码云端一键同步配置。
  - **1 - 从站模拟器模式**: 模拟 Modbus RTU/TCP 从站寄存器与动态变化曲线，支持 Web 实时联调。
  - **2 - USB 转 RS485 透传模式**: 纯粹的高速串口透明传输。
  - **3 - Modbus 协议互转模式**: 执行 Modbus TCP 与 RTU 之间的双向协议转换（TCP Server ↔ RTU / RTU ↔ TCP Client）。
  - **4 - 总线监听与质量诊断模式**: 纯被动高阻态嗅探 RS485 总线，自动配对主从应答并分析通讯成功率、RTT 响应延时、超时丢包与 CRC 误码。
  - **5 - 以太网-WiFi 桥接模式**: 跨物理网卡、跨网段的 TCP / UDP 双向流量中转。
  - **6 - 双主站中继网关模式**: 硬件双 RS485 隔离（485-1 主站侧 + 485-2 从站侧）配合 WiFi TCP 主站并发防碰撞仲裁中继。
- **全局 RS485 端口参数池**:
  - 485-1 与 485-2 的波特率、数据位、校验位、停止位在基础配置层统一定义，各业务模式按所选物理通道自动复用。
- **双网协同与安全 OTA**:
  - 支持 **W5500 硬以太网** 与 **内部 WiFi** 同时在线及底层真实网络参数穿透回显。
  - 集成生产级远程升级系统，支持 24h 自动巡检与 ESP32 原生 A/B 双固件分区一键回滚。

---

## 🔌 硬件资源分配 (`Dual485-Controller-V1`)

默认适配 **ESP32-C3** 核心板（注意：GPIO 12/13 复用 SPI Flash 引脚，编译时必须指定 `FlashMode=dio`）：

| 功能模块 | 引脚 / 协议 | ESP32-C3 引脚 | 说明 |
| :--- | :--- | :--- | :--- |
| **W5500 (SPI)** | SCK / MISO / MOSI | GPIO 12 / 10 / 13 | 高速以太网通信（需 DIO Flash 模式） |
| | CS / RST / INT | GPIO 1 / 7 / 0 | 硬件复位与中断引脚 |
| **RS485-1 (J1)** | RX / TX | GPIO 2 / 3 | 主站侧端子 / 通道 0 (`UART1` / 双主站下 `UART0`) |
| **RS485-2 (J3)** | RX / TX | GPIO 5 / 4 | 从站侧端子 / 通道 1 (`UART1`) |
| **Debug / 透传** | USB-CDC (Serial) | 原生 USB (`CDCOnBoot=cdc`) | 波特率 115200 |

---

## 🛠️ 快速开始

### 1. 依赖库 (Dependencies)
请通过 Arduino Library Manager 或 `arduino-cli` 安装以下库：
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)**: v6.x，用于 JSON 消息解析与序列化。
- **[MQTT](https://github.com/256dpi/arduino-mqtt)**: 用于 MQTT 协议接入。
- **[Ethernet](https://github.com/arduino-libraries/Ethernet)**: 适配 W5500 以太网模块（含 TCP/UDP）。
- **[HTTPClient](https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient)**: ESP32 系统自带库，用于 OTA API 与固件镜像获取。

### 2. 编译与烧录
- 默认硬件引脚与系统常量定义在 [AnyPortGateway/src/domain/AppTypes.h](file:///e:/AI%20Project/anyport-esp32/AnyPortGateway/src/domain/AppTypes.h)。
- 使用 `arduino-cli` 编译与烧录（**必须包含 `FlashMode=dio`**）：
  ```powershell
  # 编译
  arduino-cli compile --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashMode=dio,FlashSize=4M,PartitionScheme=min_spiffs" AnyPortGateway

  # 烧录 (将 COM23 替换为实际端口)
  arduino-cli upload -p COM23 --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashMode=dio,FlashSize=4M,PartitionScheme=min_spiffs" AnyPortGateway
  ```

---

## 🤝 关联项目
- **[AnyPort Web 调试与网关控制台](https://github.com/hsieh19/anyport)**: 配套的多协议工业调试助手与「🎛️ 网关控制台」前端界面（接口契约详见 [API.md](file:///e:/AI%20Project/anyport-esp32/API.md)）。

## 📜 许可证
© 2026 Hotwon-CD2-Hsieh / AnyPort Project.

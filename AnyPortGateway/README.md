## AnyPort ESP32-C3 网关固件（Arduino 工程说明）

本目录包含 AnyPort 项目的 ESP32-C3 网关固件工程：

- `AnyPortGateway.ino`：Arduino 入口文件（`setup()/loop()`）。
- `Config.h`：硬件引脚定义、外设初始化、配置存储、WebSocket 服务器、Modbus TCP/RTU 转发逻辑。

本说明文档用于指导如何在 Arduino IDE 中编译、烧录此工程，以及如何按实际硬件进行接线。

---

### 1. 开发环境要求

- **IDE**：Arduino IDE 2.x 或兼容的 Arduino 开发环境。
- **开发板核心**：ESP32 开发板支持包（Espressif Systems）。
  - 在 Arduino IDE 中：
    - 打开 “文件 → 首选项 → 附加开发板管理器网址”，添加：  
      `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
    - 打开 “工具 → 开发板 → 开发板管理器”，搜索 `esp32`，安装 **Espressif Systems** 提供的 ESP32 支持包。
  - 开发板选择示例：  
    - `ESP32C3 Dev Module`  
    - 或与你实际硬件匹配的 ESP32-C3 开发板型号。

---

### 2. 需要安装的 Arduino 库

固件依赖以下库，请通过 Arduino 库管理器安装：

1. **Ethernet**
   - 用途：通过 W5500 实现以太网（Modbus TCP 客户端）。
   - 建议安装：`Ethernet`（作者 Arduino，支持 W5100/W5200/W5500）。

2. **ArduinoJson**
   - 用途：解析与构造 WebSocket 收发的 JSON 消息（配置、Modbus 请求/响应）。
   - 建议安装：`ArduinoJson`（作者 Benoit Blanchon）。

3. **MQTT**\r\n   - 用途：实现 MQTT 客户端，连接 Broker 与前端进行双向通讯。\r\n   - 建议安装：`MQTT`（作者 Joel Gaehwiler）。

4. **ESP32 Core 自带库**
   - 这些库在安装 ESP32 核心后自动提供，无需单独安装：
     - `WiFi.h`：ESP32 WiFi STA/AP 模式。
     - `SPI.h`：SPI 总线，驱动 W5500。
     - `Preferences.h`：NVS 配置存储（WiFi/W5500/WS 端口等）。

安装完上述库后，重新打开本工程，即可正常编译。

---

### 3. 工程结构与主要功能

- `AnyPortGateway.ino`
  - 标准 Arduino 入口，调用：
    - `anyportHardwareInit()`：硬件初始化。
    - `anyportGatewayLoop()`：网关主循环（MQTT 处理 + 定期心跳 + Modbus 转发）。

- `Config.h`
  - 含完整的：
    - 引脚定义与说明；
    - W5500 SPI 与以太网初始化（支持静态 IP 配置）；
    - RS485 串口初始化（自动流控模块，无需 DE/RE）；
    - WiFi STA + AP 逻辑：
      - 优先使用 STA 连接路由器；
      - 失败时自动启用 AP，方便手机/PC 配置；
    - 配置存储（Preferences）：\r\n      - MQTT Broker 地址/端口/路径/鉴权信息；\r\n      - W5500 静态 IP/子网掩码/网关/DNS；\r\n      - WiFi STA SSID/密码；\r\n    - MQTT 通讯：\r\n      - `topic/request`：接收来自前端的 Modbus 请求或配置指令；\r\n      - `topic/response`：回传 Modbus 结果或操作状态；\r\n      - `topic/status`：发布网关在线状态与硬件信息（心跳）；
    - Modbus TCP 转发（通过 W5500 → 现场设备）；
    - Modbus RTU 转发（通过 RS485 → 现场设备）。

编译时只需在 Arduino IDE 中打开本目录（`AnyPortGateway`），IDE 会自动识别 `.ino` 为工程入口。

---

### 4. ESP32-C3 引脚定义与接线说明

以下引脚定义已经在 `Config.h` 中实现，在接线时请确保与实际硬件相符，如有变更请同步修改 `Config.h` 中对应常量。

#### 4.1 W5500 以太网模块接线

W5500 与 ESP32-C3 通过 SPI 连接，并使用独立的 CS/RST/INT 管脚。

| W5500 引脚 | 功能      | ESP32-C3 GPIO | 说明                         |
|-----------|-----------|---------------|------------------------------|
| CS        | SPI 片选  | GPIO7         | 低电平选中 W5500            |
| SCK       | SPI 时钟  | GPIO6         | SPI SCK                      |
| MISO      | SPI MISO  | GPIO5         | 从 W5500 读数据              |
| MOSI      | SPI MOSI  | GPIO4         | 向 W5500 写数据              |
| RST       | 复位      | GPIO1         | 低电平复位，高电平正常工作  |
| INT       | 中断      | GPIO3         | 可选，用于链路/数据中断检测 |
| VCC       | 电源      | 3.3V 或 5V    | 取决于模块规格（推荐 3.3V） |
| GND       | 地        | GND           | 与 ESP32 共地                |

注意：

- 如果使用的是 5V W5500 模块，请确认其 SPI 引脚已带电平转换，避免直接接 3.3V MCU 的 IO。  
- 若暂不使用 INT 中断，可将其悬空或上拉；代码中已将 INT 配置为带上拉输入。

#### 4.2 RS485 接口接线（自动流控）

本工程假定使用的 RS485 模块具备 **自动方向控制**（收发自动切换），因此 ESP32 侧只需要连接 UART TX/RX 两个信号，无需 DE/RE 控制引脚。

UART 使用 **UART1**，引脚分配如下：

| 功能       | ESP32-C3 GPIO | 连接到 RS485 模块引脚 |
|------------|---------------|------------------------|
| UART1 TXD  | GPIO21        | 模块 DI/TX            |
| UART1 RXD  | GPIO20        | 模块 RO/RX            |
| VCC        | 3.3V 或 5V    | 视模块规格            |
| GND        | GND           | 与 ESP32 共地         |

说明：

- `Config.h` 中的定义：
  - `PIN_RS485_TX = 21`  
  - `PIN_RS485_RX = 20`
- 串口初始化：`HardwareSerial RS485Serial(1);` 使用 UART1，并通过 `RS485Serial.begin(..., PIN_RS485_RX, PIN_RS485_TX)` 映射到上述管脚。
- 自动流控模块在发送时自动拉高 DE/RE，在接收时自动进入接收，无需 MCU 软件控制。

#### 4.3 WiFi 与其他资源

- WiFi 使用 ESP32-C3 内置射频，无需外部引脚；  
- 若需要调试指示灯或按键，可自行在 `Config.h` 中增加对应 GPIO 定义，并在 `initGpioPins()` 中初始化。

---

### 5. 编译与烧录步骤概览

1. 安装 ESP32 开发板核心与所需库（见前文）。  
2. 在 Arduino IDE 中选择：
   - 开发板：`ESP32C3 Dev Module`（或你的实际板型）；  
   - 合适的串口与上传速度。  
3. 打开本目录（`AnyPortGateway`），确保能看到 `AnyPortGateway.ino` 与 `Config.h`。  
4. 点击 “验证/编译”，确认无错误后点击 “上传”，完成烧录。  
5. 上电后：
   - 如已配置 WiFi STA：设备将尝试连接路由器；  
   - 如未配置或连接失败：设备会开启 AP `anyport`，密码 `12345678`，可通过浏览器访问 `192.168.4.1` 进行 Web 网页配置。

后续如需修改引脚或串口参数，请优先修改 `Config.h` 中的对应定义，并保持文档与实际硬件一致。


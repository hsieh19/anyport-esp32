# AnyPort 设备 HTTP API (v2.2)

设备端遵循前后端分离架构，只提供 RESTful JSON API（及 `GET /` 极简应急配网页），完整管理控制台已集成至 AnyPort 前端主项目的「🎛️ 网关控制台」。固件版本见 `GET /api/sys/status`。

## 通用约定

| 项 | 说明 |
| :-- | :-- |
| 基址 | `http://<设备IP>/` |
| 跨域与穿透 | 所有响应带 `Access-Control-Allow-Origin: *`；`OPTIONS` 预检返回 204 并带 `Access-Control-Allow-Private-Network: true`；公网 HTTPS 访问时可通过本地 `anyport-bridge` (`http://127.0.0.1:8081/proxy?url=...`) 代理穿透 |
| POST 请求 | **必须** `Content-Type: application/json`（包括无请求体的 POST），否则返回 `415` |
| 错误格式 | `{"ok":false,"error":"<code>"}`，HTTP 状态码 400/404/415/500/502/503 |
| 成功格式 | 写操作返回 `{"ok":true}`；需要重启生效时为 `{"ok":true,"restart":true}`，设备约 1 秒后重启 |

> **安全**：当前版本面向可信工业局域网使用，请勿将设备 HTTP 端口直接裸露映射到公网。

## 接口一览

### 系统与状态遥测
| 方法 | 路径 | 说明 |
| :-- | :-- | :-- |
| GET | `/api/sys/status` | 返回全局硬件、双网实时网络参数与总线通信遥测：`version` `firmware` `gatewayName` `mode` `rs485Channel` `wifiConnected` `wifiIp` `wifiMask` `wifiGw` `wifiDns` `wifiIsStatic` `ethLink` `ethIp` `ethMask` `ethGw` `ethDns` `ethIsStatic` `cpu` `ram`(%) `flash`(%) `rssi` `chipTemp`(℃) `uptime`(s) `heartbeatInterval` `busLatencyMs` `master1Frames` `master2Frames` `busCrcErrors`。<br>**临时维护 AP 与 IP 回显约定**：无论 DHCP 还是静态 IP 模式，网关每次上电/重启连上路由器后（以及运行中短按板载 `BOOT` 键 `GPIO 9` 时），均会开启 `WIFI_AP_STA` 临时维护热点（`anyport` / `192.168.4.1`，当 STA 子网为 `192.168.4.x` 时跳过）；前端在保存重启后可继续轮询 `/api/sys/status` 获取新分配或变更后的 `wifiIp`，当 AP 连续 3 分钟无客户端连接时自动关闭热点切换为纯 `WIFI_STA`。 |
| GET | `/api/status` | 轻量状态查询：`mode` `uptime` |
| POST | `/api/mode` | 请求体 `{"mode":0..6}`，保存工作模式并重启 |

### 配置管理
| 方法 | 路径 | 说明 |
| :-- | :-- | :-- |
| GET | `/api/config` | 读取全量配置，密码字段以 `********` 返回（未设置则为空串） |
| POST | `/api/config` | **原子部分/全量更新**：只处理出现的字段；全部校验通过后才写入 NVS，失败返回 400 且不改动任何配置；成功后重启生效 |
| POST | `/api/gateway/config` | **双主站模式 (Mode 6) 专属热重载接口**：需 `Content-Type: application/json`，支持在线下发 `gatewayName`、`proxyMode` (0:总线代理, 1:设备代理)、`slaveId` (1..247)、`baud`、`dataBits`、`parity`、`stopBits`、`wifiPort`、`masterTimeout`（或 `timeout`）、`heartbeatInterval`。采用与 `/api/config` 一致的原子参数校验（非法 JSON 或越界字段返回 400 且不写 NVS）；保存时会将串口参数**同时同步至全局 `rs485_1` 与 `rs485_2`**（Mode 6 下两路 RS485 固定同参数），并立即热重载双串口（仅在 Mode 6 触发串口重配，不影响其它模式） |

`/api/config` 字段结构（括号内为取值范围）：

```jsonc
{
  "gatewayName": "AnyPort 网关", // ≤64, 全局网关设备名称
  "workMode": 0,                 // 0:MQTT网关 1:从站模拟器 2:USB透传 3:协议互转 4:总线监听诊断 5:以太网-WiFi桥接 6:双主站中继
  "rs485Channel": 0,             // 单串口模式使用的物理通道: 0=485-1(GPIO 2/3), 1=485-2(GPIO 5/4) (模式 5/6 忽略)
  "otaApi": "https://...",       // ≤128, 以 http 开头 (留空表示未配置)
  "rs485_1": { "baud": 9600, "dataBits": 8, "parity": 0, "stopBits": 1 }, // 485-1 全局硬件参数 (GPIO 2/3)
  "rs485_2": { "baud": 9600, "dataBits": 8, "parity": 0, "stopBits": 1 }, // 485-2 全局硬件参数 (GPIO 5/4)
  "wifi": { "ssid": "", "password": "********", "ip": "", "mask": "", "gateway": "", "dns": "" }, // ip 为空串 = 自动使用 DHCP
  "eth": { "ip": "", "mask": "", "gateway": "", "dns": "" },   // ip 为空串 = 清除静态 IP
  "mqtt": { "host": "", "port": 443, "path": "/", "username": "", "password": "********",
            "topicPrefix": "", "siteId": "", "gatewayId": "" },
  "transparent": { "baud": 9600, "dataBits": 8, "parity": 0, "stopBits": 1 },
  "bridge": { "direction": 0, "mode": 0, "slaveId": 1, "netInterface": 0, "tcpPort": 502,
              "targetIp": "", "targetPort": 502, "baud": 9600, "dataBits": 8, "parity": 0, "stopBits": 1 },
  "ethWifi": { "direction": 0, "listenPort": 502, "targetIp": "", "targetPort": 502, "protocol": 0 }, // direction: 0=WiFi->W5500, 1=W5500->WiFi; protocol: 0=TCP, 1=UDP
  "dualMaster": { "gatewayName": "AnyPort 网关", "proxyMode": 0, "slaveId": 1, "baud": 9600, "dataBits": 8, "parity": 0, "stopBits": 1,
                  "wifiPort": 9502, "masterTimeout": 1000, "heartbeatInterval": 30 } // 注: proxyMode: 0=总线代理(串联接原主站出口), 1=设备代理(并联公共总线仅代理slaveId); Mode 6 下 485-1 与 485-2 共享同一套串口参数，热重载会同步覆写 rs485_1 与 rs485_2
}
```
通用范围：`baud` 1200..921600，`dataBits` 7/8，`parity` 0无/1偶/2奇，`stopBits` 1/2，端口 1..65535。
密码字段缺省、空串或 `********` 均表示保持原值。

### 从站模拟器 (Mode 1) 与总线监听诊断 (Mode 4)
| 方法 | 路径 | 说明 |
| :-- | :-- | :-- |
| GET / POST | `/api/registers` | 模拟器寄存器列表读写（POST 为整表替换，最多 32 个变量，映射至 20000 寄存器/线圈池） |
| GET / POST | `/api/simConfig` | 模拟器运行参数读写 (`rtuEnabled` `unitId` `tcpEnabled` `tcpUnitId` `tcpPort` `netInterface`) |
| GET | `/api/monitor` | 流式获取总线通讯质量统计 (`stats`：总帧数、请求数、应答数、超时无应答数、CRC 错帧数、异常响应数、当前/最小/最大/平均 RTT 耗时 ms)、监听过滤配置 (`config`) 及最近 100 条报文流 (`logs`：时间戳 `t`、身份 `meta`、从站 `uid`、功能码 `fc`、单次应答耗时 `rtt`、报文 `hex`) |
| POST | `/api/monitor/config` | 实时更新监听过滤与超时条件 (`filterSlaveId`: 0..247、`filterFuncCode`: 0..127、`timeoutMs`: 100..5000)，免重启即时生效 |
| POST | `/api/monitor/clear` | 清空环形报文缓冲区并重置全部通讯质量统计指标 |

### 远程固件更新 (OTA)
| 方法 | 路径 | 说明 |
| :-- | :-- | :-- |
| GET | `/api/ota/check` | 检查更新，返回 `found` `version` `changelog`（未配置有效地址返回 400，未联网返回 503，服务器不可达或响应无效返回 502） |
| GET | `/api/ota/status` | 最近一次检查结果与回滚分区状态：`found` `version` `canRollback` `backupVersion` |
| POST | `/api/ota/do` | 校验预签名固件链接并开始升级 |
| POST | `/api/ota/rollback` | 一键回滚至备用 OTA 分区并重启 |

## 变更记录
- **v2.2**：
  - **双主站中继网关 (Mode 6) 双拓扑架构**：新增 `proxyMode`（`0`: 总线代理 Bus Proxy，`1`: 设备代理 Device Proxy）与目标从站地址 `slaveId` (1..247) 配置；在设备代理模式下并联多从站公共总线，自动扫描剥离 `slaveId` 目标请求帧并静默旁路其他从站流量，避免误报 CRC 错误；485-1 接收超时弃用固定 15ms，改用波特率自适应计算 3.5T 字符间隔；WiFi 主站接入从站 ID 权限过滤。
  - **总线监听诊断 (Mode 4) 智能粘包切分引擎**：引入标准 Modbus PDU 结构启发式判定与 3.5T + 串口物理耗时推算；支持将密集轮询下的串口粘包自动切分为最多 16 个合法 CRC 子帧切片，并从最后一帧完成时刻向前回溯各子帧真实物理完成时间戳，彻底消除粘包对从站 RTT 与主从配对的干扰。
  - **工业级 WiFi 射频低延时与高可靠加固**：彻底关闭 Modem Sleep 省电休眠 (`WIFI_PS_NONE`)，消除 DTIM 唤醒延迟与丢包；锁定 19.5dBm 最大发射功率与 HT20 (20MHz) 窄频宽降噪；启用全信道最优 RSSI 信号择优漫游；增加 STA 掉线后每 20s 后台自愈自动重连机制。
- **v2.1**：
  - 新增全模式（DHCP + 静态 IP）开机 3 分钟临时维护 AP (`WIFI_AP_STA` · `192.168.4.1`，连续 180s 无客户端连接自动关闭，`192.168.4.x` 子网冲突时自动跳过) 与运行中板载 `BOOT` 键 (`GPIO 9`) 80ms 消抖免重启唤醒机制，配合前端控制台实现配网重启后新 IP 自动回显上报与失联找回。
  - 加固 `/api/gateway/config` 双主站热重载接口，统一要求 `Content-Type: application/json` 并执行原子参数范围校验。
  - 加固从站模拟器 Modbus PDU 边界校验（限制 FC01/02 `count <= 2000`、FC03/04 `count <= 125`、FC0F `count <= 1968`、FC10 `count <= 123` 及帧长度一致性），并为 `/api/registers` 增加 JSON 容量公式化计算与溢出 `500` 显式报错。
  - 修复双主站模式 (Mode 6) 等待从站响应期间阻塞 HTTP 请求的问题，清理单线程下冗余的自旋锁等待。
- **v2.0**：
  - 固件五层整洁架构 (Clean Architecture) 重构与前后端彻底分离。
  - 新增全局双通道 RS485 参数池 (`rs485_1` / `rs485_2`) 与 `rs485Channel` 物理通道选择。
  - 独立「模式 4：总线监听与质量诊断模式」，新增主从应答配对状态机、RTT 耗时/丢包率/CRC 误码统计与 `/api/monitor/config` 实时过滤接口。
  - 升级「模式 5：以太网-WiFi 桥接模式」，支持 `WiFi → W5500` 与 `W5500 → WiFi` 双向 × `TCP / UDP` 双协议转发 (`direction` + `protocol`)。
  - 扩展 `/api/sys/status` 全局遥测（结温 `chipTemp`、总线应答延时 `busLatencyMs`、双通道帧计数、CRC 错误计数及真实 DHCP/静态网络参数穿透回显）。
  - 加固 `/api/ota/check` 配置校验与错误码显式返回。

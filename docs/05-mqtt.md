# MQTT 遥测链路

## 组成

```
树莓派  prts-mqtt publish ──►  mosquitto (:1883)  ──►  Windows  scripts/mqtt/mqtt_listen.ps1
```

- **broker**：树莓派上的 mosquitto，监听 `0.0.0.0:1883`，匿名访问、不持久化（家庭内网）
- **publisher**：`prts-mqtt publish`，C++ 手写 MQTT 3.1.1，不依赖 libmosquitto
- **subscriber**：树莓派上的 `prts-mqtt subscribe`，或 Windows 上的 `scripts/mqtt/mqtt_listen.ps1`

## 主题约定

| 主题 | 载荷 | 方向 | 状态 |
| --- | --- | --- | --- |
| `prts/telemetry` | JSON | Pi → PC | 已完成 |
| `prts/camera/frame` | JPEG 帧 | Pi → PC | 下一步 |
| `prts/cmd/#` | 命令 | PC → Pi | 预留 |

## telemetry 载荷

```json
{"ts":"2026-11-13T21:14:59","host":"PRTS","kernel":"6.18.50+rpt-rpi-v8","uptime_s":15504.5,"cpu_temp_c":42.9,"mem_used_pct":17.9,"disk_used_pct":25.8,"load1":0.46}
```

| 字段 | 含义 |
| --- | --- |
| `ts` | 采样时刻（本地时间，秒级） |
| `host` / `kernel` | 主机名与内核版本 |
| `uptime_s` | 开机秒数 |
| `cpu_temp_c` | SoC 温度，读不到时为 `null` |
| `mem_used_pct` / `disk_used_pct` | 内存、根分区使用率 |
| `load1` | 1 分钟平均负载 |

## 运行

Windows 侧监听（默认 `prts/#`，Ctrl+C 退出）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\mqtt\mqtt_listen.ps1
powershell -ExecutionPolicy Bypass -File scripts\mqtt\mqtt_listen.ps1 -Topic 'prts/telemetry' -Count 5
```

树莓派侧发布与自测：

```bash
prts-mqtt publish --count 5 --interval 1000 --topic prts/telemetry
prts-mqtt subscribe --topic 'prts/#' --count 3
prts-mqtt --help
```

默认值可用环境变量覆盖：`PRTS_MQTT_HOST`、`PRTS_MQTT_PORT`、`PRTS_MQTT_TOPIC`。

## 实现分层

| 层 | 文件 | 职责 |
| --- | --- | --- |
| model | `model/mqtt.h` | 端点与消息结构 |
| driver | `driver/mqtt_codec.*` | 报文编解码（CONNECT / PUBLISH / SUBSCRIBE / PING / DISCONNECT，剩余长度 varint） |
| driver | `driver/tcp_mqtt_client.*` | TCP 连接、超时、keepalive、收包循环 |
| service | `service/telemetry_service.*` | 采集系统数据并组装 JSON |
| controller | `controller/mqtt_controller.*` | 命令行发布 / 订阅 |
| app | `app/mqtt_main.cpp` | 组合根，产出 `prts-mqtt` |

单元测试覆盖：剩余长度往返、CONNECT 报文结构、PUBLISH 编解码往返、不完整报文判定、CONNACK 解析、遥测 JSON 字段。

## 已知限制

- 只实现 QoS 0，没有重传与去重；局域网内够用
- 无认证、无 TLS；`/etc/mosquitto/conf.d/prts.conf` 里是匿名 + 明文，**只适合内网**
- 单条载荷过大时需要分片，mosquitto 默认上限 256 MB


## 相机画面不在这里

图传已经改成 V4L2 + FFmpeg + RTSP（mediamtx），不再把 JPEG 塞进 MQTT：

- 画面/延迟/端口：见 [../stream/README.md](../stream/README.md)
- 浏览器里的识别（WebRTC + ONNX Runtime Web）：见 [06-detect.md](06-detect.md)

MQTT 这条链路现在只负责遥测（`prts/telemetry`）。

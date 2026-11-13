# MQTT 遥测链路

## 组成

```
树莓派  prts-mqtt publish ──►  mosquitto (:1883)  ──►  Windows  scripts/mqtt_listen.ps1
```

- **broker**：树莓派上的 mosquitto，监听 `0.0.0.0:1883`，匿名访问、不持久化（家庭内网）
- **publisher**：`prts-mqtt publish`，C++ 手写 MQTT 3.1.1，不依赖 libmosquitto
- **subscriber**：树莓派上的 `prts-mqtt subscribe`，或 Windows 上的 `scripts/mqtt_listen.ps1`

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
powershell -ExecutionPolicy Bypass -File scripts\mqtt_listen.ps1
powershell -ExecutionPolicy Bypass -File scripts\mqtt_listen.ps1 -Topic 'prts/telemetry' -Count 5
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

## 相机链路

### 设备

| 项目 | 值 |
| --- | --- |
| 型号 | Microdia Integrated_Webcam_HD（USB `0c45:64ab`） |
| 驱动 | `uvcvideo`（USB Video Class，免驱） |
| 节点 | `/dev/video0`（采集）、`/dev/video1`（metadata） |
| 格式 | MJPG 640×480@30、1280×720@30；YUYV 640×480@15、1280×720@10 |
| 设备树 | **不需要**，UVC 由内核直接支持 |

`camera_auto_detect=1` 已开启，接 CSI 相机时才会用到 `dtoverlay`。

### 主题

| 主题 | 载荷 | 说明 |
| --- | --- | --- |
| `prts/camera/frame` | JPEG 原始字节（二进制） | 每帧一条，默认 640×480，约 16–19 KB |
| `prts/camera/meta` | JSON | 每 N 帧一条：`{"seq":..,"bytes":..,"format":"MJPG","width":640,"height":480,"ts":".."}` |

默认 10 fps，约 180 KB/s。按需下调 `--fps` 或分辨率。

### 树莓派：推流

```bash
prts-camera                                  # 默认 /dev/video0 640x480 10fps
prts-camera --count 100 --fps 5              # 采 100 帧后退出
prts-camera --width 1280 --height 720 --fps 5
prts-camera --device /dev/video0 --frame-topic prts/camera/frame
```

实现：`driver/v4l2_camera` 用 V4L2 mmap 四缓冲采集 MJPG，`service/camera_service` 负责生命周期与元数据，
`controller/camera_controller` 负责连接、限速与发布。

### 电脑：查看

```powershell
# 实时窗口（WinForms，无外部依赖）
powershell -ExecutionPolicy Bypass -File scripts\camera_view.ps1

# 存盘模式（无窗口，适合验证链路）
powershell -ExecutionPolicy Bypass -File scripts\camera_view.ps1 -SaveDir .\frames -Count 3
```

`-SaveDir` 模式在 `-TimeoutSeconds`（默认 15 秒）内收不到帧会退出并报错，不会挂住。

### 已知问题

- 采集到的画面接近全黑（平均亮度 2/255、最大值 5）。链路本身正常：帧是合法的 640×480 JPEG，
  所以先确认镜头是否被遮挡、是否对着暗处；`v4l2-ctl --list-ctrls` 里 `auto_exposure=3` 为自动曝光。
- USB 链路偶发 `EPROTO` 并导致设备重新枚举（`dmesg` 可见 `USB disconnect` 后立刻重连）。
  出现 `VIDIOC_S_FMT: Device or resource busy` 或 `No such device` 时，重新插拔相机即可；
  若频繁复现，换短一点的线或带供电的 HUB。


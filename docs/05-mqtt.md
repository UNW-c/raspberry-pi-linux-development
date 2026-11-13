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
| `prts/camera/frame` | JPEG 原始字节（二进制） | 每帧一条，默认 160×120，约 2.2 KB |
| `prts/camera/meta` | JSON | 每 N 帧一条：`{"seq":..,"bytes":..,"format":"MJPG","width":160,"height":120,"drained":0,"ts":".."}` |
| `prts/camera/stamp` | 毫秒时间戳 | 每帧前一条，用于端到端延迟测量 |

`drained` 是这一帧之前被丢掉的旧帧数：采集按传感器自己的 30 fps 跑，发布慢于它时
把积压的旧帧扔掉而不是排队，见下面「实时性」。

默认 `--preset realtime`：160×120、30 fps、约 64 KB/s。

### 树莓派：推流

```bash
prts-camera                          # 默认 realtime：160x120 30fps
prts-camera --preset balanced        # 320x240 20fps
prts-camera --preset sharp           # 640x480 15fps（WiFi 上会拖）
prts-camera --count 100 --fps 5      # 采 100 帧后退出
prts-camera --width 1280 --height 720 --fps 5
```

实现：`driver/v4l2_camera` 用 V4L2 mmap 四缓冲采集 MJPG（每次只交最新的一帧），
`driver/jpeg_codec` 用 libjpeg DCT 缩放重编码，`service/camera_service` 负责生命周期与元数据，
`controller/camera_controller` 负责连接、限速与发布。

### 实时性

延迟由四段决定，每一段都量过，不靠感觉：

| 环节 | 实测 | 手段 |
| --- | --- | --- |
| 传感器 → 采集完成 | ≤ 33 ms | 每次只取最新帧，积压的旧帧丢掉 |
| 重编码（640×480 → 160×120） | ~3 ms | libjpeg DCT 缩放 + 重编码 |
| 相机 → `prts-web`（本机 MQTT） | 1–2 ms | `/status` 的 `latency_ms` |
| `prts-web` → 电脑浏览器（SSH 转发） | 30–100 ms | 取决于帧大小与 WiFi 抖动 |

链路实测（树莓派 WiFi → 电脑，scp 20 MB）只有约 **230–320 KiB/s**。决定延迟的不是
`fps`，而是**单帧字节数**：一帧要全部到齐才能显示，所以 6.5 KB 的帧光传输就占
~28 ms，2.2 KB 只要 ~9 ms。链路被填满时不会变清楚，只会抖动变大。

端到端实测（`scripts/camera/measure_latency.ps1`，经 `127.0.0.1:18080`，每档 10 秒）：

| 配置 | 单帧 | 带宽 | 中位数 | p95 |
| --- | --- | --- | --- | --- |
| 160×120 q50 30 fps（默认） | 2.2 KB | 64 KiB/s | **22–45 ms** | 75–105 ms |
| 320×240 q55 20 fps | 5.4 KB | 106 KiB/s | 55 ms | 140 ms |
| 320×240 q45 30 fps | 4.8 KB | 143 KiB/s | 55 ms | 229 ms |
| 320×240 q70 15 fps（旧默认） | 6.5 KB | 99 KiB/s | 51–58 ms | ~100 ms |
| 640×480 q70 15 fps | 52 KB | 780 KiB/s | 链路打满，不可用 | — |

同一时间窗里跑三次，中位数会在 22 / 34 / 53 ms 之间跳：WiFi 抖动就是这条链路
的主要不确定性，p95 比中位数更能反映体感。页面自己算的分布（不经 PowerShell）
是中位数 31 ms、p95 79 ms。

上面量的是「采集完成 → 屏幕」；真正的玻璃到玻璃还要加上**曝光时间**
（`exposure_time_absolute` 当前 156 → 15.6 ms，暗处自动曝光会到 60 ms 以上），
所以实际约 45 ms。

结论：把带宽压到 100 KiB/s 以下，延迟就基本只剩「等下一帧」的那半个帧间隔；
再加带宽只会换来抖动。

> **坑：树莓派没有 RTC**，`date` 和电脑差 3.5 秒（靠 NTP 校准，冷启动后偏差更大）。
> 直接用 `电脑时间 - 树莓派时间戳` 会得到稳定的 3.5 s「延迟」，看着像链路积压，
> 实际是时钟差。`measure_latency.ps1` 用 `/status` 的 `server_ms` 先算时钟差再减掉，
> 网页端也是同样做法（`clockOffset`）。

三层「丢旧保新」，慢的地方不会累积延迟：

1. 采集：`VIDIOC_DQBUF` 之后继续把已完成的旧帧取出来扔掉（`drained` 计数）
2. 转发：`prts-web` 每个客户端只保留最新一帧，写不完就整帧替换
3. 浏览器：解码没跟上就丢掉这一帧（页面上的 `SKIP`），不排队

另外 `/stream` 的 socket 设了 `TCP_NODELAY` 和 16 KiB 发送缓冲，避免内核把好几帧
攒在队列里。

> 相机本身只支持 640×480 和 1280×720（MJPG / YUYV），没有更低的分辨率可选，
> 所以缩小是在树莓派上做的（需要 `libjpeg62-turbo-dev`，`provision_pi.sh` 已包含）。

调整方式：

```bash
prts-camera-web restart --preset realtime    # 默认：160x120 30fps
prts-camera-web restart --preset balanced    # 320x240 20fps
prts-camera-web restart --preset sharp       # 640x480 15fps
prts-camera-web restart --scale 2 --quality 45 --fps 25   # 也可以自己组合
```

改完立刻量一次，别猜：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\camera\measure_latency.ps1
powershell -ExecutionPolicy Bypass -File scripts\camera\measure_latency.ps1 -Seconds 20
```

### 电脑：查看

网页端（推荐，浏览器直接看，不需要任何插件）。装一次命令，之后一条指令：

```powershell
# 首次：把仓库的 bin 目录加入用户 PATH（只改当前用户，可 -Remove 撤销）
powershell -ExecutionPolicy Bypass -File scripts\setup\install_commands.ps1
# 新开一个终端后：
prts-cam                    # 拉起节点侧、开隧道、开浏览器
prts-cam status             # 看节点进程、端点状态、本机隧道
prts-cam stop               # 停掉节点侧并关闭本机隧道
prts-cam view               # 桌面窗口版
prts-cam help               # 用法
```

没装 PATH 也可以直接用：

```powershell
.\bin\prts-cam web          # 在当前目录是仓库根时
```

`prts-cam web` 做四件事（幂等，可重复执行）：

1. `ssh` 到节点执行 `prts-camera-web start`，已在跑就不重复启动
2. 用 `GET /status` 探活本机 `18080`（不是只看端口是否 LISTEN）；不通就关掉残留的
   `ssh -N` 转发并重建
3. 打开 `http://127.0.0.1:18080/`
4. 保持隧道，按 `Ctrl+C` 退出并关闭自己建立的隧道

手动方式：

```bash
# 树莓派上
prts-camera &
prts-web
```

```powershell
# 电脑上
ssh -L 127.0.0.1:18080:127.0.0.1:8080 seven@PRTS.local
# 浏览器打开 http://127.0.0.1:18080/
```

页面自带状态行：帧序号、帧率、分辨率、已绘制帧数、**端到端延迟**、浏览器丢帧数、
累计流量、观看人数、MQTT 状态。页面不用 `<img src="/stream">`，而是用 Stream API
自己解析 multipart：这样每帧都能和它的采集时间戳对比，算出的延迟是真实的
「传感器 → 屏幕」，解码跟不上时也能主动丢帧。

路由如下：

| 路由 | 内容 |
| --- | --- |
| `/` | 网页（纯内联 CSS/JS，不依赖任何 CDN） |
| `/stream` | `multipart/x-mixed-replace` MJPEG，每帧带 `X-Stamp: <毫秒>` |
| `/snapshot` | 最近一帧 JPEG |
| `/status` | `{"frames":..,"fps":..,"width":160,"height":120,"clients":..,"latency_ms":..,"dropped":..,"last_bytes":..,"stamp":..,"server_ms":..,"mqtt":"up"}` |

`server_ms` 是节点当前时间，浏览器用它换算时钟差；`stamp` 是最新一帧的采集时间。

**内网穿透场景**：`prts-web` 默认只监听 `127.0.0.1`，所以不需要在路由器或 frp 上额外开端口；
浏览器访问的是本机 `18080`，流量走已有的 SSH 连接（`ssh -L` 转发），
所以无论直连内网还是通过 frp 转发 SSH，用法完全一样。

窗口版（WinForms，直接订阅 MQTT）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\camera\camera_view.ps1
```

存盘模式（无窗口，适合验证链路，收不到帧会在 15 秒后退出）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\camera\camera_view.ps1 -SaveDir .\frames -Count 3
```

### 带宽参考

| 配置 | 单帧 | 30 fps 带宽 | 20 fps 带宽 |
| --- | --- | --- | --- |
| 160×120 q50 | 2.2 KB | 64 KiB/s | 43 KiB/s |
| 320×240 q55 | 5.4 KB | 158 KiB/s | 106 KiB/s |
| 640×480 q70 | 52 KB | 1.5 MiB/s | 1 MiB/s |

链路约 230–320 KiB/s，**留一半余量**。超了不会变清楚，只会让画面开始拖。

### 已知问题

- USB 链路偶发 `EPROTO` 并导致设备重新枚举（`dmesg` 可见 `USB disconnect` 后立刻重连）。
  出现 `VIDIOC_S_FMT: Device or resource busy` 或 `No such device` 时重新插拔相机；
  若频繁复现，换短一点的线或带供电的 HUB。
- 画面全黑时先检查镜头是否被遮挡：`auto_exposure=3`（自动）下正常室内亮度应达到 100/255 以上。
- 本机 `8080` 常被其它程序占用，因此连接脚本把本地端口映射到 `18080`；
  如需改端口，编辑 `connect_prts.ps1` 里的 `-L 127.0.0.1:18080:127.0.0.1:8080`。


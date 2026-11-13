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

网页端（推荐，浏览器直接看，不需要任何插件）。装一次命令，之后一条指令：

```powershell
# 首次：把仓库的 bin 目录加入用户 PATH（只改当前用户，可 -Remove 撤销）
powershell -ExecutionPolicy Bypass -File scripts\install_commands.ps1
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

1. `ssh` 到节点执行 `scripts/start_camera_stack.sh`，已在跑就不重复启动
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

页面自带状态行（帧序号、帧率、分辨率、流量、观看人数、MQTT 状态），
路由如下：

| 路由 | 内容 |
| --- | --- |
| `/` | 网页（纯内联 CSS/JS，不依赖任何 CDN） |
| `/stream` | `multipart/x-mixed-replace` MJPEG 实时流 |
| `/snapshot` | 最近一帧 JPEG |
| `/status` | `{"frames":..,"fps":..,"width":640,"height":480,"clients":..,"mqtt":"up"}` |

**内网穿透场景**：`prts-web` 默认只监听 `127.0.0.1`，所以不需要在路由器或 frp 上额外开端口；
浏览器访问的是本机 `18080`，流量走已有的 SSH 连接（`ssh -L` 转发），
所以无论直连内网还是通过 frp 转发 SSH，用法完全一样。

窗口版（WinForms，直接订阅 MQTT）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\camera_view.ps1
```

存盘模式（无窗口，适合验证链路，收不到帧会在 15 秒后退出）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\camera_view.ps1 -SaveDir .\frames -Count 3
```

### 带宽参考

640×480 MJPG 单帧约 74 KB，10 fps ≈ 740 KB/s；远程链路吃紧时用 `prts-camera --fps 5`
或 `--width 320 --height 240` 降档。

### 已知问题

- USB 链路偶发 `EPROTO` 并导致设备重新枚举（`dmesg` 可见 `USB disconnect` 后立刻重连）。
  出现 `VIDIOC_S_FMT: Device or resource busy` 或 `No such device` 时重新插拔相机；
  若频繁复现，换短一点的线或带供电的 HUB。
- 画面全黑时先检查镜头是否被遮挡：`auto_exposure=3`（自动）下正常室内亮度应达到 100/255 以上。
- 本机 `8080` 常被其它程序占用，因此连接脚本把本地端口映射到 `18080`；
  如需改端口，编辑 `connect_prts.ps1` 里的 `-L 127.0.0.1:18080:127.0.0.1:8080`。


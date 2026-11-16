# 图传（V4L2 + FFmpeg + RTSP）

树莓派用 V4L2 直接取相机，FFmpeg 硬件编码成 H.264，经 RTSP 送到电脑。
和 `prts-camera` 那套「JPEG 塞进 MQTT」是并存的独立项目，互不影响。

## 快速开始

电脑上双击：

```
stream\start-stream.bat
```

它会做四件事（可以重复执行，已启动的会跳过）：

1. ssh 到节点执行 `~/.local/bin/prts-stream start`
2. 开 SSH 隧道：`8889`（WebRTC 信令）、`8888`（HLS）、`8554`（RTSP）
3. 打开浏览器 `http://127.0.0.1:8889/prts`
4. 保持隧道，`Ctrl+C` 退出并关闭自己开的隧道

命令行方式：

```powershell
# 默认：WebRTC，走局域网
powershell -ExecutionPolicy Bypass -File stream\start-stream.ps1

# 强制走内网穿透（Tailscale）
powershell -ExecutionPolicy Bypass -File stream\start-stream.ps1 -Link tailscale

# 不开浏览器，只开隧道跑 60 秒（调试用）
powershell -ExecutionPolicy Bypass -File stream\start-stream.ps1 -NoBrowser -Seconds 60

# 转码参数改成 MJPG 直通（画质无损，但约 1.6 MB/s，只适合网线）
powershell -ExecutionPolicy Bypass -File stream\start-stream.ps1 -NodeOptions "--copy"
```

## 三个观看地址

| 方式 | 地址 | 延迟 | 说明 |
| --- | --- | --- | --- |
| **WebRTC（默认）** | `http://127.0.0.1:8889/prts` | 约 200–500 ms | 信令走隧道，视频走局域网 UDP 8189 |
| HLS | `http://127.0.0.1:8888/prts` | 1–3 秒 | 分片机制决定，浏览器兼容性最好 |
| RTSP | `rtsp://127.0.0.1:8554/prts` | <300 ms | 需要播放器（见下） |

电脑端**不需要装任何东西**：有 ssh 和浏览器就够了。解码是浏览器原生的。

想要更低延迟可以用 `ffplay` 直接拉 RTSP，但需要在本机装 FFmpeg：

```powershell
powershell -ExecutionPolicy Bypass -File stream\pc\install-ffmpeg.ps1   # 约 110 MB
powershell -ExecutionPolicy Bypass -File stream\pc\play-stream.ps1
```

## 节点侧命令

安装后 `~/.local/bin/prts-stream`：

```bash
prts-stream start                 # 起 mediamtx + ffmpeg（默认 H.264 4 Mbps）
prts-stream start --bitrate 6M    # 提高码率（链路要够）
prts-stream start --copy          # MJPG 直通，不重编码，约 1.6 MB/s
prts-stream start --size 1280x720 --fps 30
prts-stream stop
prts-stream restart
prts-stream status                # 进程、端口、RTSP 流参数
prts-stream logs                  # mediamtx 和 ffmpeg 的日志
```

`start` 会先停掉 `prts-camera`，因为它占着 `/dev/video0`。

## 安装（节点）

```bash
# 在仓库目录里执行
bash stream/node/install_stream.sh

# 如果 mediamtx 还没装：先在电脑上下载，再传过去
bash stream/node/install_stream.sh --mediamtx /tmp/mediamtx_v1.21.4_linux_arm64.tar.gz
```

- `ffmpeg` 由 apt 安装（Tsinghua 源）
- `mediamtx` 不在 apt 里，GitHub 在节点上又连不通，所以必须先从电脑拷贝；
  电脑侧下载地址：`https://github.com/bluenviron/mediamtx/releases`（选 `linux_arm64`）
- `deploy.sh` 会自动把 `prts-stream` 装到 `~/.local/bin`

## 实测数据

节点上量，不经过网络（`stream/node/bench_capture.sh`）：

| 方式 | 速率 | 说明 |
| --- | --- | --- |
| MJPG `-c copy` 直通 | 1597 KiB/s | 画质无损，30 fps |
| H.264 硬编码 2 Mbps | 132 KiB/s | `h264_v4l2m2m`，ffmpeg 约 66% 单核 |
| H.264 硬编码 4 Mbps | 132 KiB/s | 静态画面，编码器不会填满码率 |
| H.264 硬编码 6 Mbps | 139 KiB/s | 同上 |

整条链路：

| 场景 | 结果 |
| --- | --- |
| RTSP 流本身（`ffprobe`） | h264 640×480 30 fps |
| 10 秒实际码率 | 124 KiB/s，278 帧（约 27.8 fps） |
| 局域网隧道取 HLS 分片 | 350 KiB / 2 秒画面，0.62 s 下完（568 KiB/s） |
| Tailscale 上行 | 467 KiB/s |
| 编码器 CPU | 约 66% 单核（共 4 核） |

链路约 900 KiB/s（局域网）/ 467 KiB/s（Tailscale），流只要 124–175 KiB/s。

## 结构

```
stream/
├── start-stream.ps1 / .bat   PC 一级启动脚本
├── node/
│   ├── install_stream.sh     节点：装 ffmpeg、放 mediamtx、装 prts-stream
│   ├── prts-stream.sh        节点：启停 mediamtx + ffmpeg
│   └── bench_capture.sh      节点：v4l2+ffmpeg 采集/编码基准
└── pc/
    ├── install-ffmpeg.ps1    可选：本机装 ffplay（约 110 MB）
    └── play-stream.ps1       可选：ffplay 拉 RTSP 播放
```

## 踩过的坑

1. **HLS 慢是分片机制决定的**：2 秒分片 + 播放器预缓冲，3 秒延迟里九成在这里，
   不是链路。要低延迟就用 WebRTC 或 ffplay。
2. **ssh 断开会把后台进程一起杀掉**：`nohup` 只挡 SIGHUP，sshd 断开会 SIGTERM
   整个会话组。`prts-stream` 用 `setsid` 让它脱离会话。
3. **mediamtx 的 `index.m3u8` 返回 302**：就绪探测必须 `-L` 跟跳转并检查 `#EXTM3U`，
   否则永远判定「没起来」。
4. **WebRTC 的视频不走 ssh**：信令走隧道，媒体是浏览器直连节点的 UDP 8189。
   局域网没问题；内网穿透下如果一直黑屏，说明 UDP 没打通，需要退回 TCP 或改用 HLS。
5. **相机偶尔 `Dequeued v4l2 buffer contains corrupted data`**：USB 带宽偶尔不够，
   单帧损坏，不影响播放。

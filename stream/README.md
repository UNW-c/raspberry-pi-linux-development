# 图传（V4L2 + FFmpeg + RTSP）

用 FFmpeg 直接从 V4L2 取流，经 RTSP 送到电脑，替代原来「JPEG 塞进 MQTT」的做法。

## 目录

```
stream/
├── start-stream.ps1      # PC 端一级启动脚本：起节点推流 + 开隧道 + 开浏览器
├── start-stream.bat      # 双击入口
├── node/
│   ├── bench_capture.sh  # 采集/编码基准（不涉及网络）
│   ├── install_stream.sh # 节点上装 ffmpeg、放 mediamtx
│   └── prts-stream.sh    # 节点上启停 mediamtx + ffmpeg
└── pc/
```

## 阶段一：V4L2 + FFmpeg（已完成）

`node/bench_capture.sh` 在节点上跑，不经过网络：

| 方式 | 速率 | 说明 |
| --- | --- | --- |
| `-c copy`（MJPG 直通） | **1597 KiB/s** | 画质无损，30 fps，五秒 149 帧 |
| H.264 硬编码 2 Mbps | 132 KiB/s | `h264_v4l2m2m`，ffmpeg 占用约 66% 单核 |
| H.264 硬编码 4 Mbps | 132 KiB/s | 同上（画面静止时编码器不会填满码率） |
| H.264 硬编码 6 Mbps | 139 KiB/s | 同上 |

结论：**MJPG 直通在这条 WiFi 上跑不动**（1597 KiB/s 远超链路的约 900 KiB/s），
H.264 硬编码只要 130–500 KiB/s，画质远好于原来的 160×120 JPEG。

## 阶段二：RTSP（进行中）

节点跑 `mediamtx`（RTSP 服务器），ffmpeg 推流上去，电脑通过 SSH 隧道取：

```
ffmpeg(v4l2) --RTSP--> mediamtx --HLS/RTSP--> ssh 隧道 --> 浏览器 / 播放器
```

端口：`8554` RTSP、`8888` HLS（浏览器用）。都只监听 `127.0.0.1`，靠隧道暴露。

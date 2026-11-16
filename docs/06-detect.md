# 浏览器端识别（奶龙）

识别在**浏览器页面里**跑，画面就是图传那一路 WebRTC——同一次解码、同一帧，
所以框和画面天然对齐，也不需要第二条视频流。

```
WebRTC video（浏览器解码）
   ↓ MediaStreamTrackProcessor（Chromium）
VideoFrame ──transfer──▶ Web Worker
                          ↓ letterbox 416×416，0–255
                        ONNX Runtime Web（WASM，多线程）
                          ↓ YOLOX 网格解码 + 逐类 NMS
主线程 ◀── {boxes} ────────┘
   ↓
叠加层 canvas 画框（和 video 同尺寸、同帧）
```

## 打开

```powershell
stream\start-stream.bat        # 先起图传（隧道 + 推流）
stream\start-detect-web.bat    # 再开识别页，默认 http://127.0.0.1:8891/
```

需要 **Chrome / Edge**：`MediaStreamTrackProcessor` 只有 Chromium 系提供。
不支持时会退回 `VideoFrame(video)` + `requestVideoFrameCallback`（多一次拷贝，
但能用）。

## 页面底部读数

| 字段 | 含义 |
| --- | --- |
| `WHEP` | 连接状态（connected / failed） |
| `VIDEO` | 视频分辨率 |
| `FRAMES` | 收到的已解码帧数 |
| `FPS` | 画面帧率（应该是 30 左右） |
| `DETECT` | 推理次数 @ 推理帧率 |
| `NOTE` | 加载/推理的错误原文，出问题先看这里 |

## 模型

用的是 `models/nailong_v2.6_tiny.onnx`（YOLOX，20 MB，AGPL-3.0，
来自 [nkxingxh/NailongDetection](https://github.com/nkxingxh/NailongDetection)），
由 `serve.py` 以 `/models/` 映射出去。

运行时是 **onnxruntime-web 1.20.1**（微软官方，MIT），约 11 MB，
放在 `stream/web/vendor/ort/`，**不入库**；换机器先跑：

```powershell
powershell -ExecutionPolicy Bypass -File stream\web\fetch_ort.ps1
```

标签 8 类：`_background_ nailong anime human emoji long other htgt`。

## 踩过的坑

1. **输入必须是 0–255**。YOLOX 的 ONNX 导出没有把归一化放进图里，喂 `img/255`
   时目标置信度从 0.99 掉到 0.14，等于完全不检测。
2. **mediamtx 不回 CORS 预检**，页面不能跨端口直接发 WHEP。`serve.py` 做同源
   `/whep` 代理解决。
3. **WASM 多线程要 `SharedArrayBuffer`**，只有跨源隔离（COOP/COEP）的页面才有。
   `serve.py` 两个头都发了；没隔离时自动退回单线程。
4. **页面必须和 WHEP 同源**，否则预检失败；所以是 `serve.py` 托管页面而不是直接用
   mediamtx 的播放页——它的播放页没法注入我们的 JS。

## 实测

| 项目 | 数值 |
| --- | --- |
| 画面 | 640×480，30 fps（浏览器 WebRTC） |
| 识别 | Worker 推理，帧率见页面 `DETECT` |
| 网络 | 只走 WebRTC 一路，没有额外视频流 |

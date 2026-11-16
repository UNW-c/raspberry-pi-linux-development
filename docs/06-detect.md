# 奶龙检测（电脑端）

## 结论先说

用 [nkxingxh/NailongDetection](https://github.com/nkxingxh/NailongDetection)：
基于 **YOLOX**，发布了 ONNX 模型（`nailong_v2.6_tiny`，20 MB，416×416）。
它识别的不只是奶龙，标签一共 8 类：

```
_background_ nailong anime human emoji long other htgt
```

模型用 **AGPL-3.0** 许可，只下载到本机 `models/`（不入库）。

同类项目对比：

| 项目 | 模型 | 能否画框 |
| --- | --- | --- |
| nkxingxh/NailongDetection | YOLOX tiny/m，ONNX | 能，多类别 |
| SendsTeam/nailong-detection | ResNet18 分类器 | 不能，只输出「是/不是」 |

## 为什么跑在电脑上

树莓派 3B 是 4×Cortex-A53 @1.2 GHz、905 MB 内存。同一个 416×416 的 YOLOX-tiny：

| 机器 | 单帧推理 |
| --- | --- |
| 电脑（OpenCV DNN，8 线程） | **约 77 ms** |
| 树莓派 3B（估） | 1 s 以上，且抢相机链路的 CPU |

相机链路本来就在意延迟，检测器放上去只会两边都卡，所以检测放在电脑。

## 安装与运行

依赖只有 **opencv-python + numpy**，不需要 onnxruntime：OpenCV 自带 ONNX 导入器。
脚本会复用已经装好这两个包的 Python 环境，找不到才建 `.venv-detect`。

```powershell
# 双击
scripts\detect\detect.bat

# 或者指定解释器
powershell -ExecutionPolicy Bypass -File scripts\detect\run_detect.ps1 -Python C:\Users\me\myenv\Scripts\python.exe
```

常用参数（透传给 `detect_nailong.py`）：

```powershell
... run_detect.ps1                              # 弹窗显示带框画面
... run_detect.ps1 --no-window --count 30       # 只跑 30 帧，只打日志
... run_detect.ps1 --http 18081                 # 另外开一个带框的网页流
... run_detect.ps1 --score 0.5 --scale 6        # 提高阈值、放大窗口
... run_detect.ps1 --source snapshot            # 退回旧相机页面取帧
```

## 画面来源

默认读 **RTSP 图传**（`rtsp://127.0.0.1:8554/prts`），也就是 `stream\start-stream.bat`
开的那条隧道，拿到的是 640×480 原始画面。所以顺序是：先 `start-stream`，再跑检测。

| `--source` | 取帧方式 | 依赖 |
| --- | --- | --- |
| `rtsp`（默认） | RTSP 流，640×480 | `stream\start-stream.bat` |
| `snapshot` | `http://127.0.0.1:18080/snapshot`，160×120 | `prts-cam`（旧 MQTT 链路） |
| `auto` | 先试 RTSP，失败退回 snapshot | 都行 |

两条链路在相机上是互斥的（`/dev/video0` 只能被一个进程占用），所以不要指望同时开。

## 踩过的坑

1. **输入必须是 0–255**。YOLOX 的 ONNX 导出没有把归一化放进图里，喂 `img/255`
   时目标置信度从 0.99 掉到 0.14，等于完全检测不到。
2. **urllib 会走系统代理**。Windows 上 Python 从注册表读代理设置，请求
   `127.0.0.1` 也会被转发出去，表现为一直超时。`ProxyHandler({})` 关掉即可。
3. **RTSP 必须用 TCP**。OpenCV 的 ffmpeg 后端默认用 UDP，UDP 过不了 SSH 隧道，
   表现是卡 30 秒后报 `Stream timeout`。要用环境变量指定，而且必须在 Python
   启动前就设好：
   ```
   OPENCV_FFMPEG_CAPTURE_OPTIONS=rtsp_transport;tcp|stimeout;5000000
   ```
   `run_detect.ps1` 已经替你设了。
4. **隧道关掉后不能崩**。读帧线程里必须捕 `cv2.error` 并重连，否则线程静默死掉、
   画面永远停住。现在断了会打印 `stream lost, reconnecting ...` 并每 3 秒重试。

## 实测（本机，2026-11-16）

| 项目 | 数值 |
| --- | --- |
| 模型加载 | 约 0.3 s |
| 推理 | 77 ms / 帧（8 线程） |
| 读 RTSP（含首次连接约 12 s） | 640×480，30 fps 源 |
| 整体 | **约 10 fps**（推理为瓶颈，不是链路） |
| 示例图（`images/examples.png`） | `long` 0.91、`anime` 0.88 |

换成 RTSP 之后帧率上限从「链路能送多少张 160×120」变成「模型能算多快」，
所以稳定在 10 fps 左右，与画面复杂度和链路抖动无关。

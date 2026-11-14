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
```

默认从 `http://127.0.0.1:18080/snapshot` 取帧，也就是 `prts-cam` 开的那条隧道；
所以先让相机网页能打开，再跑检测。

## 踩过的坑

1. **输入必须是 0–255**。YOLOX 的 ONNX 导出没有把归一化放进图里，喂 `img/255`
   时目标置信度从 0.99 掉到 0.14，等于完全检测不到。
2. **urllib 会走系统代理**。Windows 上 Python 从注册表读代理设置，请求
   `127.0.0.1` 也会被转发出去，表现为一直超时。`ProxyHandler({})` 关掉即可。
3. **取最新帧而不是排队**：轮询 `/snapshot`，同一帧重复就直接跳过，慢的时候
   自动降频，不会越拖越久。

## 实测（本机，2026-11-14）

| 项目 | 数值 |
| --- | --- |
| 模型加载 | 约 0.3 s |
| 推理 | 77 ms / 帧（8 线程） |
| 整体 | 受链路限制，好链路下约 5–8 fps，链路差时降到 1 fps |
| 示例图（`images/examples.png`） | `long` 0.91、`anime` 0.88 |

整体帧率的上限不是模型，是相机帧过来的速度：检测要等下一条 `/snapshot`。

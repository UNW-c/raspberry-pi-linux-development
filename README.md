# PRTS 终端服务

树莓派 Linux 开发工作区。项目用于学习 Linux 用户态与内核态开发，第一个子系统是 **PRTS 登录自检动画**：
每次通过 SSH 登录树莓派后，终端播放一段主机终端风格的引导序列 —— 纯黑白灰、发丝细线、
`╱╲` 绘制的菱形源石徽标（由一圈扩散的涟漪生成）、带时间戳的七行自检日志、
黑白反转的当前行、进度条与接入记录，数据全部来自真实的 procfs/sysfs。

版式参考社区整理的 [PRTS Design](https://mooncellwiki.github.io/prts-design/)
（MooncellWiki 对《明日方舟》终端 / 档案体系的梳理）：黑白灰承载信息、直角与细线、
拉丁大写与编号作装饰、菱形是源石母题、强调用明度反转而不是彩色。
终端里只出现设定中存在的名字（`PRTS`、`RHODES ISLAND`）和实测数据，不添加说明性文案。
实现不使用任何游戏素材，全部字符由网格算法生成。

远端仓库：

- Gitee: <https://gitee.com/bababba/raspberry-pi-linux-development>
- GitHub: <https://github.com/UNW-c/raspberry-pi-linux-development>

## 目标设备

| 项目 | 值 |
| --- | --- |
| 型号 | Raspberry Pi 3 Model B Rev 1.2 |
| 系统 | Debian GNU/Linux 13 (trixie), aarch64 |
| 内核 | 6.18.50+rpt-rpi-v8（头文件已就位） |
| 主机名 / 用户 | PRTS / seven |
| 内存 / 存储 | 905 MiB / 14 GiB 根分区 |

## 快速开始

连接入口就在工作区内，双击 `scripts\connect\connect_prts.bat` 即可（会自己调大控制台窗口）：

```powershell
# 1. 建立连接：双击 scripts\connect\connect_prts.bat，或命令行执行
powershell -ExecutionPolicy Bypass -File scripts\connect\connect_prts.ps1

# 2. 把工程同步到树莓派并编译安装
powershell -ExecutionPolicy Bypass -File scripts\deploy\sync_to_pi.ps1
```

之后重新 SSH 登录即可看到 PRTS 引导序列。临时跳过：

```bash
PRTS_SKIP_BOOT=1 ssh seven@PRTS.local
```

## 目录结构

```
.
├── app/           应用层     main.cpp，组合根，负责依赖装配
├── controller/    控制层     参数解析、运行模式决策
├── service/       服务层     动画、自检、配置、会话状态、序列编排
├── model/         模型层     纯数据：颜色、系统快照、自检结果、配置结构
├── driver/        驱动层     终端 ANSI 实现、时钟、Linux 系统探针
├── utils/         工具层     UTF-8 文本宽度、ANSI 序列、日志、字符串切分
├── config/        配置层     prts.conf（动画）与 connection.ini（连接）
├── test/          测试层     自包含测试框架 + 假实现
├── scripts/       工具脚本   按用途分目录：lib / connect / deploy / camera / mqtt / setup
├── bin/           命令入口   prts-cam.cmd（加入 PATH 后全局可用）
└── docs/          文档       环境、架构、动画规格、工作流、MQTT
```

依赖方向严格自上而下：`app → controller → service → model`，`service` 通过接口依赖
`driver`，`driver` 只依赖 `utils` 与 `model`。下层不认识上层，替换终端或探针实现不影响动画逻辑。

## 命令行

```bash
prts-boot              # 播放引导序列（交互式 TTY）
prts-boot --check      # 只做自检并输出纯文本报告
prts-boot --force      # 配置或环境变量禁用时仍强制执行
prts-boot --config F   # 追加一个配置文件

prts-mqtt publish      # 采集系统数据并发到 MQTT（prts/telemetry）
prts-mqtt subscribe    # 订阅主题并把消息打印出来

prts-camera            # V4L2 采集 MJPG 帧并发到 prts/camera/frame
prts-camera --count 50 --fps 5

prts-web               # 把帧转成网页 /stream（默认 127.0.0.1:8080）
prts-mqtt --help       # 帮助
```

## 脚本

脚本按用途分目录，见 [scripts/README.md](scripts/README.md)。

| 目录 | 文件 | 运行位置 | 作用 |
| --- | --- | --- | --- |
| `scripts/connect/` | `connect_prts.bat` / `.ps1` | Windows | 双击即连：解析主机、自动填密码、开 SSH 会话 |
| `scripts/deploy/` | `sync_to_pi.ps1` | Windows | 打包 → scp → 远端解包 → 编译 → 安装 |
| `scripts/deploy/` | `push_all.ps1` | Windows | 提交并同时推送 gitee 与 github |
| `scripts/deploy/` | `deploy.sh` | 树莓派 | CMake 编译、跑单测、安装二进制、写登录钩子 |
| `scripts/camera/` | `camera_web.bat` / `.ps1` | Windows | 双击即看：拉起节点侧、开隧道、开浏览器 |
| `scripts/camera/` | `camera_view.ps1` | Windows | 桌面窗口版，或 `-SaveDir` 存盘 |
| `scripts/camera/` | `prts-camera-web.sh` | 树莓派 | 安装为 `prts-camera-web` 命令，启停相机与网页 |
| `scripts/camera/` | `measure_latency.ps1` | Windows | 量端到端帧年龄（经隧道，含时钟校准） |
| `scripts/camera/` | `inspect_camera.sh` | 树莓派 | 列出 USB / V4L2 / 设备树里的相机信息 |
| `scripts/mqtt/` | `mqtt_listen.ps1` | Windows | 订阅主题并按行显示 |
| `scripts/detect/` | `detect.bat` / `run_detect.ps1` | Windows | 双击跑奶龙检测（复用已有 Python 环境） |
| `scripts/detect/` | `detect_nailong.py` | Windows | YOLOX 推理 + 画框 + 可选网页流 |
| `scripts/detect/` | `download_model.ps1` | Windows | 下载 ONNX 模型到 `models/`（不入库） |
| `scripts/setup/` | `install_commands.ps1` | Windows | 把 `bin` 加入用户 PATH（`prts-cam` 全局可用） |
| `scripts/setup/` | `provision_pi.sh` | 树莓派 (root) | 安装工具链与依赖、建目录、导出 locale |
| `bin/` | `prts-cam.cmd` | Windows | 命令行入口：`prts-cam [web\|view\|status\|stop]` |

## MQTT 遥测

树莓派上跑 mosquitto，节点用 `prts-mqtt` 往 `prts/telemetry` 发 JSON，
电脑上用 `scripts/mqtt/mqtt_listen.ps1` 收：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\mqtt\mqtt_listen.ps1 -Topic 'prts/#' -Count 5
```

```json
{"ts":"2026-11-13T21:14:59","host":"PRTS","kernel":"6.18.50+rpt-rpi-v8","uptime_s":15504.5,"cpu_temp_c":42.9,"mem_used_pct":17.9,"disk_used_pct":25.8,"load1":0.46}
```

MQTT 客户端是按 MQTT 3.1.1 手写的（`driver/mqtt_codec` + `driver/tcp_mqtt_client`），
不依赖 libmosquitto。详见 [MQTT 遥测链路](docs/05-mqtt.md)。

## 相机

树莓派上插的 USB 相机（UVC，免驱）由 `prts-camera` 采集成 MJPEG 帧发到 MQTT，
`prts-web` 再把帧转成网页的 MJPEG 流。

相机只支持 640×480 / 1280×720，而 WiFi 只有约 230–320 KiB/s。决定延迟的是**单帧字节数**：
一帧必须全部到齐才能显示，所以默认在树莓派上重编码到 160×120（约 2.2 KB，30 fps，
共约 64 KiB/s），端到端延迟中位数 **约 30–45 ms**（含 15 ms 曝光）。要更清楚的画面就换 preset，
代价是延迟变大（实测见 [docs/05-mqtt.md](docs/05-mqtt.md)）：

```bash
prts-camera-web restart --preset realtime   # 默认 160x120 30fps，~34 ms
prts-camera-web restart --preset balanced   # 320x240 20fps，~55 ms
prts-camera-web restart --preset sharp      # 640x480 15fps，链路打满
```

```bash
# 树莓派
prts-camera &
prts-web
```

```powershell
# 装一次命令（加入用户 PATH），之后任意目录可用
powershell -ExecutionPolicy Bypass -File scripts\setup\install_commands.ps1

prts-cam            # 浏览器看画面（自动拉起节点侧 + 开隧道）
prts-cam status     # 节点进程 / 端点 / 本机隧道
prts-cam stop       # 停掉
# 浏览器地址：http://127.0.0.1:18080/
```

页面底部会显示 `LATENCY`：浏览器自己按帧上的采集时间戳算出的真实端到端延迟
（树莓派没有 RTC，页面会先用 `/status` 的 `server_ms` 校准时钟差）。

想单独量一遍：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\camera\measure_latency.ps1
```

因为网页服务只监听节点的 `127.0.0.1`、通过 SSH 本地转发访问，
所以直连内网和通过内网穿透（frp）连 SSH 时，浏览器里的地址和用法完全一样。

## 奶龙检测

电脑端跑 YOLOX（[nkxingxh/NailongDetection](https://github.com/nkxingxh/NailongDetection)，
ONNX，AGPL-3.0），从相机页面取帧、画框，树莓派不参与推理：

```powershell
scripts\detect\detect.bat                 # 弹窗显示带框画面
scripts\detect\detect.bat --http 18081    # 或者开个带框的网页流
```

依赖只有 opencv + numpy（OpenCV 自带 ONNX 导入器），脚本会复用机器上已有的
Python 环境。模型下载到 `models/`，不入库。细节见 [docs/06-detect.md](docs/06-detect.md)。

## 私有设备地址

Tailscale 的 IP、之后的 WiFi 名称这类信息放在 `config/devices.private.ini`，
该文件在 `.gitignore` 里，不会进仓库。连接、部署、相机脚本会自动读取它，
把 Tailscale 地址作为局域网名字不通时的兜底：

```powershell
# 探测顺序：PRTS -> PRTS.local -> Tailscale 地址
powershell -File scripts\connect\connect_prts.ps1
```

## 配置

`config/prts.conf` 是默认配置，部署时复制到 `~/.config/prts/prts.conf`（已存在则不覆盖）。
加载优先级：内置默认值 → 系统默认文件 → 用户配置 → `--config`。

常用键：

```ini
[config]
schema = 3                ; 版本号，部署时据此迁移旧配置

[general]
char_delay_ms = 6         ; 逐字速度
frame_delay_ms = 24       ; 进度条帧间隔
check_delay_ms = 70       ; 每条自检的停顿
min_columns_full = 62     ; 低于此宽度使用紧凑布局
min_rows_full = 27        ; 低于此高度使用紧凑布局
flash_frames = 1          ; 自检行的黑白反转帧数，0 关闭

[colors]
rule = #2E2E2E            ; 细线
primary = #F0F0F0         ; 正文
muted = #8A8A8A           ; 标签
warn = #C9A227            ; 仅告警
danger = #C0392B          ; 仅故障
```

## 开发工作流

1. 本地编辑源码（工作区即 git 仓库）。
2. `powershell -File scripts\deploy\sync_to_pi.ps1` 同步并编译。
3. 重新登录 SSH 验证动画；`prts-boot --check` 只看数据。
4. 提交并推送（一次完成本地提交 + 两个远端）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\deploy\push_all.ps1 -Message "feat: ..."
```

也可以手动执行：

```powershell
git add -A
git commit -m "feat: ..."
git push gitee main
$env:HTTPS_PROXY = "http://127.0.0.1:10808"   # GitHub 需要本地代理
git push github main
```

> 备注：本机 git 需使用 OpenSSL TLS 后端（`git config --global http.sslBackend openssl`），
> 否则 schannel 在受限环境下会报 `SEC_E_NO_CREDENTIALS`。GitHub 推送依赖
> `config/connection.ini` 中 `[git] github_proxy` 配置的本地代理。

## 文档

- [环境与工具链](docs/01-environment.md)
- [分层架构](docs/02-architecture.md)
- [PRTS 引导序列规格](docs/03-prts-boot-animation.md)
- [日常开发流程](docs/04-workflow.md)

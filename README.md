# PRTS 终端服务

树莓派 Linux 开发工作区。项目用于学习 Linux 用户态与内核态开发，第一个子系统是 **PRTS 登录自检动画**：
每次通过 SSH 登录树莓派后，终端会播放一段参考《明日方舟》PRTS 系统风格的引导序列 —— 菱形徽标、
方块字标、扫描线、逐行自检、进度条与状态栏，数据全部来自真实的 procfs/sysfs。

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

```powershell
# 1. 建立连接（双击桌面 PRTS 快捷方式，或直接执行）
powershell -ExecutionPolicy Bypass -File scripts\connect_prts.ps1

# 2. 首次使用：创建桌面快捷方式
powershell -ExecutionPolicy Bypass -File scripts\create_shortcut.ps1

# 3. 把工程同步到树莓派并编译安装
powershell -ExecutionPolicy Bypass -File scripts\sync_to_pi.ps1
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
├── scripts/       工具脚本   Windows 连接/同步、树莓派配置/部署
└── docs/          文档       环境、架构、动画规格、工作流
```

依赖方向严格自上而下：`app → controller → service → model`，`service` 通过接口依赖
`driver`，`driver` 只依赖 `utils` 与 `model`。下层不认识上层，替换终端或探针实现不影响动画逻辑。

## 命令行

```bash
prts-boot              # 播放引导序列（交互式 TTY）
prts-boot --check      # 只做自检并输出纯文本报告
prts-boot --force      # 配置或环境变量禁用时仍强制执行
prts-boot --config F   # 追加一个配置文件
prts-boot --help       # 帮助
prts-boot --version    # 版本
```

## 脚本

| 脚本 | 运行位置 | 作用 |
| --- | --- | --- |
| `scripts/connect_prts.ps1` / `.cmd` | Windows | 解析主机、自动填充密码、打开 SSH 会话，失败时给出诊断且不重试 |
| `scripts/create_shortcut.ps1` | Windows | 在桌面创建 PRTS 快捷方式 |
| `scripts/sync_to_pi.ps1` | Windows | 打包 → scp → 远端解包 → 编译 → 安装 |
| `scripts/push_all.ps1` | Windows | 提交并同时推送 gitee 与 github（自动带上代理） |
| `scripts/provision_pi.sh` | 树莓派 (root) | 安装工具链与编译依赖、建立目录、导出 locale |
| `scripts/deploy.sh` | 树莓派 (seven) | CMake 编译、跑单测、安装二进制、写入登录钩子 |

## 配置

`config/prts.conf` 是默认配置，部署时复制到 `~/.config/prts/prts.conf`（已存在则不覆盖）。
加载优先级：内置默认值 → 系统默认文件 → 用户配置 → `--config`。

常用键：

```ini
[general]
char_delay_ms = 10        ; 逐字速度
frame_delay_ms = 30       ; 扫描线帧间隔
check_delay_ms = 90       ; 每条自检的停顿
min_columns_full = 78     ; 低于此宽度使用紧凑布局
min_rows_full = 30        ; 低于此高度使用紧凑布局

[colors]
accent = #11AFCF          ; PRTS 青色强调色
```

## 开发工作流

1. 本地编辑源码（工作区即 git 仓库）。
2. `powershell -File scripts\sync_to_pi.ps1` 同步并编译。
3. 重新登录 SSH 验证动画；`prts-boot --check` 只看数据。
4. 提交并推送（一次完成本地提交 + 两个远端）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\push_all.ps1 -Message "feat: ..."
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

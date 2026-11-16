# 日常开发流程

## 1. 连接

- 在工作区内双击 `scripts\connect\connect_prts.bat`，或执行
  `powershell -ExecutionPolicy Bypass -File scripts\connect\connect_prts.ps1`
- 脚本按 `config/connection.ini` 依次尝试 `PRTS.local`、`PRTS` 等候选，先把名字解析成 IP
  再探测 22 端口（避免冷启动时 mDNS 慢导致误判），命中后通过 `SSH_ASKPASS` 把密码交给 ssh
- 自动把控制台窗口调整为 120x38（仅经典控制台有效，Windows Terminal 忽略该请求）
- 只有一次尝试：失败会打印「哪一步失败 + 检查顺序」，不会循环重连

> 历史坑：早期版本用 `start wt.exe ...` 打开 Windows Terminal 再转发命令，若本机的
> Windows Terminal 应用别名损坏（`%LOCALAPPDATA%\Microsoft\WindowsApps\wt.exe` 变成 0 字节），
> 每次启动都会弹 `shell:AppsFolder\Microsoft.WindowsTerminal_...!App 无法访问指定设备、路径或文件`。
> 现在启动链只有 `bat → powershell -File`，不再经过 wt.exe 或 cmd `start`。

排障顺序：

1. 树莓派是否上电（电源灯常亮、绿色活动灯闪烁）
2. 同一网段（当前 `192.168.150.x`）
3. `ping PRTS.local`（mDNS，由 avahi 提供）
4. mDNS 不可用时，把固定 IP 写进 `config/connection.ini` 的 `fallback_host`

### 链路选择

局域网和内网穿透（Tailscale）同时可用时，默认走局域网：

```powershell
connect_prts.ps1                      # auto：局域网优先，Tailscale 兜底
connect_prts.ps1 -Link lan            # 只用局域网
connect_prts.ps1 -Link tailscale      # 强制走内网穿透
```

`sync_to_pi.ps1`、`camera_web.ps1`、`start-stream.ps1` 都有同名参数。

坑：开了 MagicDNS 后 `PRTS` 会解析成 `100.x`（Tailscale 网段），ssh 和隧道里所有流量
都会绕道穿透，实测带宽只有局域网的几分之一。所以解析时把 `100.64.0.0/10` 排到最后，
只有在局域网名字全都不通时才用它。

局域网地址、Tailscale 地址、WiFi 名称都放在 `config/devices.private.ini`（不入库）。

## 2. 开发：代码怎么到树莓派上

本地只负责写，编译与运行都在 Pi 上完成。三条路线，当前默认第一条：

| 路线 | 做法 | 适用 |
| --- | --- | --- |
| A 本地写 + 同步编译 | `tar` 打包 → `scp` → 远端 `cmake` 构建 → 安装 | 日常开发（默认） |
| B 直接登录编辑 | `ssh` 后在 Pi 上用 `vim` 改文件再本地编译 | 临时改一行、现场排查 |
| C 交叉编译 | x86 上用 aarch64 工具链编译，只传二进制 | 没板子时离线构建，需要 sysroot |

默认走 A 的原因：目标是 aarch64，交叉编译要额外维护工具链和 sysroot；Pi 上已经有
gcc 14 / cmake 3.31；同步只传源码（排除 `.git`、`build`），一次约 40 KiB。

### A 的执行链

Windows 侧 `scripts/deploy/sync_to_pi.ps1`：

1. 读 `config/connection.ini`：`host` / `fallback_host` / `user` / `password` / `port` /
   `[project] remote_path`
2. 解析主机（DNS → mDNS），对 22 端口做 TCP 探测，选第一个可达的地址
3. 打包：`tar.exe -czf %TEMP%\prts-source.tar.gz -C <仓库> --exclude=.git --exclude=build ...`
4. 上传：`scp -P 22 <archive> seven@<ip>:/tmp/prts-source.tar.gz`
5. 远端执行一条命令：
   `mkdir -p ~/raspberry-pi-linux-development && tar -xzf /tmp/... -C ... && cd ... && bash scripts/deploy/deploy.sh`

树莓派侧 `scripts/deploy/deploy.sh`：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4        # 4 核
ctest --output-on-failure               # 15 项单元测试
install -m 0755 build/prts-boot ~/.local/bin/prts-boot
# 配置 schema 比对：不一致则备份旧文件并安装新的
# 幂等写入 ~/.bashrc 登录钩子
prts-boot --check | tail -n 12          # 冒烟测试
```

之后重新登录时，`~/.bashrc` 钩子执行 `~/.local/bin/prts-boot` 播放引导序列
（条件：交互式 shell、`stdout` 是 TTY、未设 `PRTS_SKIP_BOOT`、本次会话未播过）。

### 一次改动的闭环

以「改一条自检项的文案」为例：

```powershell
# 1. 改 service/selfcheck_service.cpp 与 test/test_selfcheck.cpp
# 2. 同步并编译（实测：改一个源文件约 12.6 s，无改动约 5.2 s）
powershell -ExecutionPolicy Bypass -File scripts\deploy\sync_to_pi.ps1
# 3. 验证：重新登录，或在 Pi 上执行 prts-boot --force
# 4. 提交并推送两个远端
powershell -ExecutionPolicy Bypass -File scripts\deploy\push_all.ps1 -Message "feat: ..."
```

只上传不编译（改配置、看文档）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\deploy\sync_to_pi.ps1 -SkipBuild
```

跳过测试（明确知道只是改文档/注释）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\deploy\sync_to_pi.ps1 -SkipTests
```

### B 路线：直接登录改

```bash
ssh seven@PRTS.local
cd ~/raspberry-pi-linux-development
vim service/boot_sequence_service.cpp
bash scripts/deploy/deploy.sh --skip-tests
```

改完记得在本地工作区同步回来（否则下次同步会被覆盖）：把改动在本地重做一遍，或
`scp seven@PRTS.local:~/raspberry-pi-linux-development/service/xxx.cpp service/`。

### C 路线：交叉编译

需要一个 aarch64 工具链（如 `aarch64-linux-gnu-g++`）与 Pi 的 sysroot：

```powershell
cmake -S . -B build-arm64 -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ `
  -DCMAKE_SYSROOT=<sysroot>
cmake --build build-arm64
scp build-arm64/prts-boot seven@PRTS.local:~/.local/bin/
```

代价是要自己维护工具链版本与 sysroot；本项目暂时不需要。

### 出错时看哪里

| 现象 | 含义 | 处理 |
| --- | --- | --- |
| `scp failed with exit code 1` | 主机不可达或密码错 | 先跑 `connect_prts.ps1` 看链路面板 |
| `remote step failed with exit code 8` | `ctest` 有失败用例 | 看输出里的 `FAIL:` 行，或登录后 `./build/prts_tests` |
| 编译报错 | 缺头文件 / 漏加源文件 | 检查 `CMakeLists.txt` 的源文件清单与 `#include` |
| 登录没有动画 | 钩子未生效、非 TTY、`PRTS_SKIP_BOOT` 已设 | `bash -i` 手动测试，确认 `~/.local/bin/prts-boot` 可执行 |
| 运行时报错 | 二进制有问题 | 看 `~/.local/state/prts/prts-boot.log`，`PRTS_LOG_LEVEL=debug` 提高级别 |

## 3. 验证

```bash
prts-boot --check      # 只要数据报告，不播放动画
prts-boot --force      # 即使 PRTS_SKIP_BOOT 已设置也播放
PRTS_SKIP_BOOT=1 ssh ...   # 跳过动画
```

动画只在「交互式 + TTY」时播放，因此 scp/rsync/git-over-ssh 不会被污染。

## 4. 提交

```powershell
git add -A
git commit -m "feat(boot): 调整扫描线速度并补充自检行格式说明"
git push gitee main
$env:HTTPS_PROXY = "http://127.0.0.1:10808"   # GitHub 需要代理
git push github main
```

提交信息写清「改了什么 + 完成了什么任务」，一次提交只做一件事。

远端配置：

| 远端 | 地址 | 认证方式 | 备注 |
| --- | --- | --- | --- |
| gitee | `git@gitee.com:bababba/raspberry-pi-linux-development.git` | SSH 公钥（已配置） | 直连可用 |
| github | `https://github.com/UNW-c/raspberry-pi-linux-development` | HTTPS + 凭据管理器 | 需先启动本地代理 |

## 5. 常见问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 双击后弹 `shell:AppsFolder\Microsoft.WindowsTerminal_...!App 无法访问指定设备、路径或文件` | 本机 Windows Terminal 应用别名损坏（`wt.exe` 为 0 字节），旧版脚本转发命令时触发 | 启动链已不再使用 wt.exe；如其它工具受影响，可在 Microsoft Store 里修复/重装 Windows Terminal |
| `git push` 报 `SEC_E_NO_CREDENTIALS` | Windows schannel 在受限环境取不到凭证 | `git config --global http.sslBackend openssl` |
| GitHub 报 `Connection was reset` | 未走代理，连接被重置 | 启动代理并把 `HTTPS_PROXY` 指向其端口（系统代理当前为 `127.0.0.1:10808`） |
| `apt-get` 下载只有几十 KB/s | 官方源在境外 | 已切到清华镜像，原文件备份在 `/root/debian.sources.prts-backup` |
| 登录后没有动画 | 非交互 shell / 未安装二进制 / 已设置 `PRTS_SKIP_BOOT` | 检查 `~/.bashrc` 钩子与 `~/.local/bin/prts-boot` |
| 中文显示为方块 | 本地终端字体缺中文字形 | Windows Terminal 换用支持中文的等宽字体 |
| 版式错位 | 终端宽度不足或字体非等宽 | 保持 ≥ 78x30，或用紧凑版式 |
| 编译 OOM | Pi 3 只有 905 MiB 内存 | `cmake --build build --parallel 2`，或加大交换分区 |

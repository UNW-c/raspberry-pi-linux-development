# 日常开发流程

## 1. 连接

- 双击桌面 `PRTS` 快捷方式，或执行 `scripts\connect_prts.cmd`
- 脚本按 `config/connection.ini` 依次探测 `PRTS`、`PRTS.local`、`PRTS.local` 的 22 端口，
  命中后通过 `SSH_ASKPASS` 把密码交给 ssh，无需手输
- 只有一次尝试：失败会打印「哪一步失败 + 检查顺序」，不会循环重连

排障顺序：

1. 树莓派是否上电（电源灯常亮、绿色活动灯闪烁）
2. 同一网段（当前 `192.168.210.x`）
3. `ping PRTS.local`（mDNS，由 avahi 提供）
4. mDNS 不可用时，把固定 IP 写进 `config/connection.ini` 的 `fallback_host`

## 2. 开发

源码在工作区里改。分层约定见 [分层架构](02-architecture.md)：

- 新增数据字段 → `model/`
- 新增外部读取 → `driver/`，并先在接口里声明能力
- 新增业务规则 → `service/`
- 只改版式或节奏 → `service/boot_sequence_service.cpp` 与 `service/logo_art.cpp`

本地无法直接编译 aarch64，用同步脚本在 Pi 上构建：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\sync_to_pi.ps1
```

流程：`tar` 打包（排除 `.git`、`build`）→ `scp` 到 `/tmp` → 远端解包到
`/home/seven/raspberry-pi-linux-development` → `scripts/deploy.sh`
（`cmake` 构建 → `ctest` → 安装到 `~/.local/bin` → 写登录钩子 → 冒烟测试）。

只上传不构建：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\sync_to_pi.ps1 -SkipBuild
```

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
| `git push` 报 `SEC_E_NO_CREDENTIALS` | Windows schannel 在受限环境取不到凭证 | `git config --global http.sslBackend openssl` |
| GitHub 报 `Connection was reset` | 未走代理，连接被重置 | 启动代理并把 `HTTPS_PROXY` 指向其端口（系统代理当前为 `127.0.0.1:10808`） |
| `apt-get` 下载只有几十 KB/s | 官方源在境外 | 已切到清华镜像，原文件备份在 `/root/debian.sources.prts-backup` |
| 登录后没有动画 | 非交互 shell / 未安装二进制 / 已设置 `PRTS_SKIP_BOOT` | 检查 `~/.bashrc` 钩子与 `~/.local/bin/prts-boot` |
| 中文显示为方块 | 本地终端字体缺中文字形 | Windows Terminal 换用支持中文的等宽字体 |
| 版式错位 | 终端宽度不足或字体非等宽 | 保持 ≥ 78x30，或用紧凑版式 |
| 编译 OOM | Pi 3 只有 905 MiB 内存 | `cmake --build build --parallel 2`，或加大交换分区 |

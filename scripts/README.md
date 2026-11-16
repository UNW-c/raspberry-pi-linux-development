# 脚本目录

按用途分类。PowerShell 脚本都用「从 `$PSScriptRoot` 向上找 `CMakeLists.txt`」的方式定位仓库根，
所以移动目录后不需要改内部路径。

相机图传和识别是独立项目，在仓库根目录的 `stream/` 下，见 [../stream/README.md](../stream/README.md)。

```
scripts/
├── lib/                 共享模块（被其它脚本 dot-source）
│   ├── prts_remote.ps1  连接解析、链接面板、密码注入、HTTP 探活、隧道清理
│   └── prts_mqtt.ps1    二进制安全的 MQTT 3.1.1 客户端（Windows 侧）
├── connect/             SSH 连接
│   ├── connect_prts.bat 双击即连
│   └── connect_prts.ps1 实现
├── deploy/              构建、同步、提交
│   ├── sync_to_pi.ps1   Windows：打包 → scp → 远端编译安装
│   ├── deploy.sh        树莓派：cmake / ctest / install / 登录钩子
│   └── push_all.ps1     提交并推送 gitee + github
├── mqtt/                MQTT 工具
│   └── mqtt_listen.ps1  订阅并按行显示
├── setup/               环境准备
│   └── provision_pi.sh  树莓派工具链与依赖（root 执行一次）
└── boot/                登录动画（由 deploy.sh 安装，暂无独立脚本）
```

## 常用入口

| 场景 | 命令 |
| --- | --- |
| 连树莓派 | 双击 `scripts\connect\connect_prts.bat`（会先让你选局域网 / 内网穿透） |
| 指定链路连 | `connect_prts.bat -Link lan` 或 `-Link tailscale` |
| 同步并编译 | `powershell -File scripts\deploy\sync_to_pi.ps1` |
| 提交并推送 | `powershell -File scripts\deploy\push_all.ps1 -Message "..."` |
| 看图传（PC） | 双击 `stream\start-stream.bat` |
| 看识别（PC） | 双击 `stream\start-detect-web.bat` |
| 起/停推流（树莓派） | `prts-stream start` / `stop` / `status` / `logs` |
| 订阅遥测 | `powershell -File scripts\mqtt\mqtt_listen.ps1 -Topic 'prts/#'` |

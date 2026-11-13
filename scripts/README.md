# 脚本目录

按用途分类。PowerShell 脚本都用「从 `$PSScriptRoot` 向上找 `CMakeLists.txt`」的方式定位仓库根，
所以移动目录后不需要改内部路径。

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
├── camera/              相机链路
│   ├── prts-camera-web.sh  树莓派命令（安装为 ~/.local/bin/prts-camera-web）
│   ├── camera_web.ps1      Windows：拉起节点侧 + 开隧道 + 开浏览器
│   ├── camera_web.bat      双击入口
│   ├── prts_cam.ps1        Windows 命令行分发（web/view/status/stop）
│   ├── camera_view.ps1     桌面窗口 / 存盘
│   ├── measure_latency.ps1 Windows：量端到端帧年龄（经隧道，先校准节点时钟）
│   └── inspect_camera.sh   树莓派：相机诊断
├── mqtt/                MQTT 工具
│   └── mqtt_listen.ps1  订阅并按行显示
├── setup/               环境准备
│   ├── provision_pi.sh  树莓派工具链与依赖
│   └── install_commands.ps1  把 bin/ 加入用户 PATH
└── boot/                登录动画（由 deploy.sh 安装，暂无独立脚本）
```

## 常用入口

| 场景 | 命令 |
| --- | --- |
| 连树莓派 | 双击 `scripts\connect\connect_prts.bat` |
| 同步并编译 | `powershell -File scripts\deploy\sync_to_pi.ps1` |
| 提交并推送 | `powershell -File scripts\deploy\push_all.ps1 -Message "..."` |
| 看相机网页（PC） | 双击根目录 `camera_web.bat`，或 `prts-cam` |
| 起/停相机（树莓派） | `prts-camera-web start` / `stop` / `status` |
| 换画质档位（树莓派） | `prts-camera-web restart --preset balanced` |
| 量端到端延迟（PC） | `powershell -File scripts\camera\measure_latency.ps1` |
| 装命令到 PATH | `powershell -File scripts\setup\install_commands.ps1` |

# 环境与工具链

## 硬件与系统

| 项目 | 值 | 说明 |
| --- | --- | --- |
| 型号 | Raspberry Pi 3 Model B Rev 1.2 | 4 x Cortex-A53，aarch64 |
| 发行版 | Debian GNU/Linux 13 (trixie) | 官方 64 位镜像 |
| 内核 | 6.18.50+rpt-rpi-v8 | 启动链由树莓派固件 + U-Boot 接管 |
| 内核头文件 | `/usr/src/linux-headers-6.18.50+rpt-rpi-v8` | 已安装，可直接构建外部模块 |
| 内存 | 905 MiB 可见（GPU 划分 76 MiB） | 编译时建议 `-j4` 而不是更高 |
| 存储 | 14 GiB 根分区，约 11 GiB 可用 | CMake 构建目录约 30 MiB |

## 已安装工具链

`scripts/setup/provision_pi.sh` 会保证以下软件存在（幂等，只装缺失项）：

- 编译：`build-essential`(gcc 14 / g++ 14 / make 4.4)、`cmake`、`gdb`、`device-tree-compiler`
- 版本控制与传输：`git`、`rsync`、`curl`
- 交互：`vim`、`tmux`、`htop`、`tree`
- 运行环境：`python3`、`locales`

执行方式（需要 root）：

```bash
echo <密码> | sudo -S bash scripts/setup/provision_pi.sh --user seven --ssh-key /tmp/prts_key.pub
```

脚本会顺带：

1. 建立 `~/.local/bin`、`~/.local/state/prts`、`~/.config/prts`
2. 在 `~/.profile` 写入 `LANG=C.UTF-8`（原系统 `LANG` 为空，会影响 UTF-8 排版与工具输出）
3. 可选：把 Windows 公钥写入 `~/.ssh/authorized_keys`（连接脚本随后可免密）

## 编码注意事项

- 树莓派默认 `LANG` 为空，`locale -a` 只有 `C` / `C.utf8` / `en_GB.utf8`。
  动画依赖 UTF-8 字节直出，本身不受影响，但 `ls`、`vim` 等工具需要 `LANG=C.UTF-8`。
- 全部源码、配置、脚本使用 UTF-8 无 BOM；`.bats/.sh` 使用 LF 换行。
- 终端需要等宽字体才能对齐：Windows Terminal 默认 Cascadia Mono 表现良好。

## 内核态开发预备

内核头文件与 `linux-kbuild` 已安装，可直接编译外部模块：

```bash
make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

后续若要做设备树实验，`device-tree-compiler` 提供 `dtc`；树莓派专用覆盖文件位于
`/boot/firmware/overlays`。
